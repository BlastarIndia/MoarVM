#include "moarvm.h"
static void process_worklist(MVMThreadContext *tc, MVMGCWorklist *worklist);

/* Garbage collects the nursery. This is a semi-space copying collector,
 * but only copies very young objects. Once an object is seen/copied once
 * in here (may be tuned in the future to twice or so - we'll see) then it
 * is not copied to tospace, but instead promoted to the second generation,
 * which is managed through mark-compact. Note that it adds the roots and
 * processes them in phases, to try to avoid building up a huge worklist. */
void MVM_gc_nursery_collect(MVMThreadContext *tc) {
    MVMGCWorklist *worklist;
    void *fromspace;
    void *tospace;
    
    /* Swap fromspace and tospace. */
    fromspace = tc->nursery_tospace;
    tospace   = tc->nursery_fromspace;
    tc->nursery_fromspace = fromspace;
    tc->nursery_tospace   = tospace;
    
    /* Reset nursery allocation pointers to the new tospace. */
    tc->nursery_alloc       = tospace;
    tc->nursery_alloc_limit = (char *)tc->nursery_alloc + MVM_NURSERY_SIZE;
    
    /* Create a GC worklist. */
    worklist = MVM_gc_worklist_create(tc);
    
    /* Add permanent roots and process them. */
    MVM_gc_root_add_parmanents_to_worklist(tc, worklist);
    process_worklist(tc, worklist);
    
    /* Add temporary roots and process them. */
    MVM_gc_root_add_temps_to_worklist(tc, worklist);
    process_worklist(tc, worklist);
    
    /* Add things that are roots for the first generation because
     * they are pointed to by objects in the second generation and
     * process them. */
    MVM_gc_root_add_gen2s_to_worklist(tc, worklist);
    process_worklist(tc, worklist);
    
    /* Find roots in frames and process them. */
    if (tc->cur_frame)
        MVM_gc_root_add_frame_roots_to_worklist(tc, worklist, tc->cur_frame);
    process_worklist(tc, worklist);

    /* Destroy the worklist. */
    MVM_gc_worklist_destroy(tc, worklist);
}

/* Processes the current worklist. */
static void process_worklist(MVMThreadContext *tc, MVMGCWorklist *worklist) {
    MVMGen2Allocator  *gen2;
    MVMCollectable   **item_ptr;
    MVMCollectable    *new_addr;
    MVMuint32          size;
    MVMuint16          i;
    
    /* Grab the second generation allocator; we may move items into the
     * old generation. */
    gen2 = tc->instance->gen2;
    
    while (item_ptr = MVM_gc_worklist_get(tc, worklist)) {
        /* Dereference the object we're considering. */
        MVMCollectable *item = *item_ptr;

        /* If the item is NULL, that's fine - it's just a null reference and
         * thus we've no object to consider. */
        if (item == NULL)
            continue;

        /* If it's in the second generation, we have nothing to do. */
        if (item->flags & MVM_CF_SECOND_GEN)
            continue;
        
        /* If we already saw the item and copied it, then it will have a
         * forwarding address already. Just update this pointer to the
         * new address and we're done. */
        if (item->forwarder) {
            *item_ptr = item->forwarder;
            continue;
        }

        /* If the pointer is already into tospace, we already updated it,
         * so we're done. */
        if (item >= tc->nursery_tospace && item < tc->nursery_alloc_limit)
            continue;
            
        /* At this point, we know we're going to be copying the object, but
         * we don't know where. Work out the size. */
        if (!(item->flags & (MVM_CF_TYPE_OBJECT | MVM_CF_STABLE | MVM_CF_SC)))
            size = ((MVMObject *)item)->st->size;
        else if (item->flags & MVM_CF_TYPE_OBJECT)
            size = sizeof(MVMObject);
        else if (item->flags & MVM_CF_STABLE)
            size = sizeof(MVMSTable);
        else if (item->flags & MVM_CF_SC)
            MVM_panic(15, "Can't handle serialization contexts in the GC yet");
        else
            MVM_panic(15, "Internal error: impossible case encountered in GC sizing");
        
        /* Did we see it in the nursery before? */
        if (item->flags & MVM_CF_NURSERY_SEEN) {
            /* Yes; we should move it to the second generation. Allocate
             * space in the second generation. */
            new_addr = MVM_gc_gen2_allocate(gen2, size);
            
            /* Copy the object to the second generation and mark it as
             * living there. */
            memcpy(new_addr, item, size);
            new_addr->flags ^= MVM_CF_NURSERY_SEEN;
            new_addr->flags |= MVM_CF_SECOND_GEN;
        }
        else {
            /* No, so it will live in the nursery for another GC
             * iteration. Allocate space in the nursery. */
            new_addr = (MVMCollectable *)tc->nursery_alloc;
            tc->nursery_alloc = (char *)tc->nursery_alloc + size;
            
            /* Copy the object to tospace and mark it as seen in the
             * nursery (so the next time around it will move to the
             * older generation, if it survives). */
            memcpy(new_addr, item, size);
            new_addr->flags |= MVM_CF_NURSERY_SEEN;
        }
        
        /* Store the forwarding pointer and update the original
         * reference. */
        *item_ptr = item->forwarder = new_addr;
        
        /* Add the serialization context address to the worklist. */
        MVM_gc_worklist_add(tc, worklist, &new_addr->sc);
        
        /* Otherwise, we need to do the copy. What sort of thing are we
         * going to copy? */
        if (!(item->flags & (MVM_CF_TYPE_OBJECT | MVM_CF_STABLE | MVM_CF_SC))) {
            /* Need to view it as an object in here. */
            MVMObject *new_addr_obj = (MVMObject *)new_addr;
            
            /* Add the STable to the worklist. */
            MVM_gc_worklist_add(tc, worklist, &new_addr_obj->st);
            
            /* If needed, mark it. This will add addresses to the worklist
             * that will need updating. Note that we are passing the address
             * of the object *after* copying it since those are the addresses
             * we care about updating; the old chunk of memory is now dead! */
            if (REPR(new_addr_obj)->gc_mark)
                REPR(new_addr_obj)->gc_mark(tc, STABLE(new_addr_obj), OBJECT_BODY(new_addr_obj), worklist);
        }
        else if (item->flags & MVM_CF_TYPE_OBJECT) {
            /* Add the STable to the worklist. */
            MVM_gc_worklist_add(tc, worklist, &((MVMObject *)new_addr)->st);
        }
        else if (item->flags & MVM_CF_STABLE) {
            /* Add all references in the STable to the work list. */
            MVMSTable *new_addr_st = (MVMSTable *)new_addr;
            MVM_gc_worklist_add(tc, worklist, &new_addr_st->HOW);
            MVM_gc_worklist_add(tc, worklist, &new_addr_st->WHAT);
            MVM_gc_worklist_add(tc, worklist, &new_addr_st->method_cache);
            for (i = 0; i < new_addr_st->vtable_length; i++)
                MVM_gc_worklist_add(tc, worklist, &new_addr_st->vtable[i]);
            for (i = 0; i < new_addr_st->type_check_cache_length; i++)
                MVM_gc_worklist_add(tc, worklist, &new_addr_st->type_check_cache[i]);
            if (new_addr_st->container_spec) {
                MVM_gc_worklist_add(tc, worklist, &new_addr_st->container_spec->value_slot.class_handle);
                MVM_gc_worklist_add(tc, worklist, &new_addr_st->container_spec->value_slot.attr_name);
                MVM_gc_worklist_add(tc, worklist, &new_addr_st->container_spec->fetch_method);
            }
            if (new_addr_st->boolification_spec)
                MVM_gc_worklist_add(tc, worklist, &new_addr_st->boolification_spec->method);
            MVM_gc_worklist_add(tc, worklist, &new_addr_st->WHO);
            
            /* If it needs to have it's REPR data marked, do that. */
            if (new_addr_st->REPR->gc_mark_repr_data)
                new_addr_st->REPR->gc_mark_repr_data(tc, new_addr_st, worklist);
        }
        else if (item->flags & MVM_CF_SC) {
            MVM_panic(15, "Can't copy serialization contexts in the GC yet");
        }
        else {
            MVM_panic(15, "Internal error: impossible case encountered in GC copy");
        }
    }
}

/* Some objects, having been copied, need no further attention. Others
 * need to do some additional freeing, however. This goes through the
 * fromspace and does any needed work to free uncopied things (this may
 * run in parallel with the mutator, which will be operating on tospace). */
void MVM_gc_nursery_free_uncopied(MVMThreadContext *tc, void *limit) {
    /* We start scanning the fromspace, and keep going until we hit
     * the end of the area allocated in it. */
    void *scan = tc->nursery_fromspace;
    while (scan < limit) {
        /* The object here is dead if it never got a forwarding pointer
         * written in to it. */
        MVMCollectable *item = (MVMCollectable *)scan;
        MVMuint8 dead = item->forwarder == NULL;

        /* Now go by collectable type. */
        if (!(item->flags & (MVM_CF_TYPE_OBJECT | MVM_CF_STABLE | MVM_CF_SC))) {
            /* Object instance. If dead, call gc_free if needed. Scan is
             * incremented by object size. */
            MVMObject *obj = (MVMObject *)item;
            if (dead && REPR(obj)->gc_free)
                REPR(obj)->gc_free(tc, obj);
            scan = (char *)scan + STABLE(obj)->size;
        }
        else if (item->flags & MVM_CF_TYPE_OBJECT) {
            /* Type object. See if we need to free REPR data. Scan is
             * incremented by type object size. */
            MVMObject *obj = (MVMObject *)item;
            if (REPR(obj)->gc_free_repr_data)
                REPR(obj)->gc_free_repr_data(tc, STABLE(obj));
            scan = (char *)scan + sizeof(MVMObject);
        }
        else if (item->flags & MVM_CF_STABLE) {
            /* Dead STables are a little interesting. Of course, there is
             * stuff to free, but there's also an ordering issue: we need
             * to make sure we don't toss these until we freed up all the
             * other things, since the size data held in the STable may be
             * needed in order to finish walking the fromspace! So we will
             * add them to a list and then free them all at the end. */
            if (dead) {
                MVM_panic(15, "Can't free STables in the GC yet");
            }
            scan = (char *)scan + sizeof(MVMSTable);
        }
        else if (item->flags & MVM_CF_SC) {
            MVM_panic(15, "Can't free serialization contexts in the GC yet");
        }
        else {
            printf("item flags: %d\n", item->flags);
            MVM_panic(15, "Internal error: impossible case encountered in GC free");
        }
    }
    
    /* Zero out the fromspace. This is not strictly needed, but it's a good
     * safety measure. */
    memset(tc->nursery_fromspace, 0, MVM_NURSERY_SIZE);
}