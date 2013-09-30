#include "moarvm.h"

static const MVMREPROps this_repr;

const MVMREPROps * MVMCStruct_initialize(MVMThreadContext *tc) {
    return &this_repr;
}

static MVMObject * type_object_for(MVMThreadContext *tc, MVMObject *HOW) {
    MVMSTable *st = MVM_gc_allocate_stable(tc, &this_repr, HOW);

    MVMROOT(tc, st, {
        MVMObject *obj = MVM_gc_allocate_type_object(tc, st);
        MVM_ASSIGN_REF(tc, st, st->WHAT, obj);
        st->size = sizeof(MVMPtr);
    });

    return st->WHAT;
}

static MVMObject * allocate(MVMThreadContext *tc, MVMSTable *st) {
    return MVM_gc_allocate_object(tc, st);
}

static void initialize(MVMThreadContext *tc, MVMSTable *st, MVMObject *root,
        void *data) {
    MVMPtrBody *body = data;

    if (!st->REPR_data)
        MVM_exception_throw_adhoc(tc,
                "cannot initialize C struct from uncomposed type object");

    body->cobj = NULL;
    body->blob = NULL;
}

static int find_name(const void *key, const void *value) {
    return MVM_string_compare(NULL, (MVMString *)key, *(MVMString **)value);
}

static MVMint64 hint_for(MVMThreadContext *tc, MVMSTable *st,
        MVMObject *class_handle, MVMString *name) {
    MVMCStructSpec *spec = st->REPR_data;

    MVMString **value = bsearch(name, spec->member_names, spec->member_count,
            sizeof(MVMString *), find_name);

    return value ? value - spec->member_names : MVM_NO_HINT;
}

static void get_attribute(MVMThreadContext *tc, MVMSTable *st,
        MVMObject *root, void *data, MVMObject *class_handle, MVMString *name,
        MVMint64 hint, MVMRegister *result, MVMuint16 kind) {
    MVMPtrBody *body = data;
    MVMCStructSpec *spec = st->REPR_data;
    MVMObject *type;
    MVMuint64 offset;

    if (hint < 0) {
        hint = hint_for(tc, st, class_handle, name);
        if (hint < 0)
            MVM_exception_throw_adhoc(tc, "unknown attribute");
    }

    type = spec->members[hint].type;
    offset = spec->members[hint].offset;

    switch (kind) {
        case MVM_reg_obj:
            MVMROOT(tc, root, {
                MVMPtr *ptr = (MVMPtr *)MVM_repr_alloc_init(tc, type);
                ptr->body.cobj = (char *)body->cobj + offset;
                MVM_ASSIGN_REF(tc, ptr, ptr->body.blob, body->blob);
                result->o = (MVMObject *)ptr;
            });
            return;

        case MVM_reg_int64: {
            MVMCScalarSpec *scalar_spec;

            if (STABLE(type)->REPR->ID != MVM_REPR_ID_CScalar)
                MVM_exception_throw_adhoc(tc, "cannot get int from non-scalar");

            scalar_spec = STABLE(type)->REPR_data;

            result->i64 = scalar_spec->fetch_int(tc,
                    (char *)body->cobj + offset);
            return;
        }

        case MVM_reg_num64: {
            MVMCScalarSpec *scalar_spec;

            if (STABLE(type)->REPR->ID != MVM_REPR_ID_CScalar)
                MVM_exception_throw_adhoc(tc, "cannot get num from non-scalar");

            scalar_spec = STABLE(type)->REPR_data;

            result->n64 = scalar_spec->fetch_num(tc,
                    (char *)body->cobj + offset);
            return;
        }

        default:
            MVM_exception_throw_adhoc(tc,
                    "unsupported result kind for C struct get_attribute");
    }
}

static void bind_attribute(MVMThreadContext *tc, MVMSTable *st,
        MVMObject *root, void *data, MVMObject *class_handle, MVMString *name,
        MVMint64 hint, MVMRegister value, MVMuint16 kind) {
    MVMPtrBody *body = data;
    MVMCStructSpec *spec = st->REPR_data;
    MVMObject *type;
    MVMuint64 offset;

    if (hint < 0) {
        hint = hint_for(tc, st, class_handle, name);
        if (hint < 0)
            MVM_exception_throw_adhoc(tc, "unknown attribute");
    }

    type = spec->members[hint].type;
    offset = spec->members[hint].offset;

    switch (kind) {
        case MVM_reg_obj:
            MVM_exception_throw_adhoc(tc, "TODO [%s:%u]", __FILE__, __LINE__);

        case MVM_reg_int64: {
            MVMCScalarSpec *scalar_spec;

            if (STABLE(type)->REPR->ID != MVM_REPR_ID_CScalar)
                MVM_exception_throw_adhoc(tc, "cannot put int into non-scalar");

            scalar_spec = STABLE(type)->REPR_data;

            scalar_spec->store_int(tc,
                    (char *)body->cobj + offset, value.i64);
            return;
        }

        case MVM_reg_num64: {
            MVMCScalarSpec *scalar_spec;

            if (STABLE(type)->REPR->ID != MVM_REPR_ID_CScalar)
                MVM_exception_throw_adhoc(tc, "cannot put int into non-scalar");

            scalar_spec = STABLE(type)->REPR_data;

            scalar_spec->store_num(tc,
                    (char *)body->cobj + offset, value.n64);
            return;
        }

        default: fail:
            MVM_exception_throw_adhoc(tc,
                    "unsupported argument kind for C struct bind_attribute");
    }
}

static MVMint64 is_attribute_initialized(MVMThreadContext *tc, MVMSTable *st,
        void *data, MVMObject *class_handle, MVMString *name, MVMint64 hint) {
    return 1;
}

static MVMStorageSpec get_storage_spec(MVMThreadContext *tc, MVMSTable *st) {
    MVMStorageSpec spec;

    spec.inlineable      = MVM_STORAGE_SPEC_REFERENCE;
    spec.boxed_primitive = MVM_STORAGE_SPEC_BP_NONE;
    spec.can_box         = 0;

    return spec;
}

static void gc_mark(MVMThreadContext *tc, MVMSTable *st, void *data,
        MVMGCWorklist *worklist) {
    MVMPtrBody *body = data;

    if (body->blob)
        MVM_gc_worklist_add(tc, worklist, &body->blob);
}

static void gc_mark_repr_data(MVMThreadContext *tc, MVMSTable *st,
        MVMGCWorklist *worklist) {
    MVMCStructSpec *spec = st->REPR_data;

    if (spec) {
        MVMuint64 i;

        for (i = 0; i < spec->member_count; i++) {
            MVM_gc_worklist_add(tc, worklist, &spec->member_names[i]);
            MVM_gc_worklist_add(tc, worklist, &spec->members[i].type);
        }
    }

}

static void gc_free_repr_data(MVMThreadContext *tc, MVMSTable *st) {
    MVMCStructSpec *spec = st->REPR_data;

    if (spec) {
        free(spec->member_names);
        free(spec->members);
        MVM_checked_free_null(st->REPR_data);
    }
}

static void compose(MVMThreadContext *tc, MVMSTable *st, MVMObject *info) {
    MVMCStructSpec *spec;
    MVMuint64 count = MVM_repr_elems(tc, info);
    MVMuint64 size = 0, align = 0;
    MVMuint64 i;

    if (count < 2)
        MVM_exception_throw_adhoc(tc, "cannot create empty C struct");

    spec = malloc(sizeof *spec);
    spec->member_count = count / 2;
    spec->member_names = malloc(count * sizeof(MVMString *));
    spec->members = malloc(count * sizeof(MVMCMemberSpec));

    for (i = 0; i < count; i += 2) {
        MVMObject *type = MVM_repr_at_pos_o(tc, info, i);
        MVMString *name = MVM_repr_get_str(tc,
                MVM_repr_at_pos_o(tc, info, i + 1));
        MVMuint64 member_size  = MVM_native_csizeof(tc, type);
        MVMuint64 member_align = MVM_native_calignof(tc, type);
        MVMuint64 padding = size % member_align
                ? member_align - size % member_align
                : 0;
        MVMuint64 pos = i / 2;

        if (member_align > align)
            align = member_align;

        size += padding;

        /* insertion sort */
        while (pos > 0 && MVM_string_compare(tc, name,
                spec->member_names[pos - 1]) < 0) {
            spec->member_names[pos] = spec->member_names[pos - 1];
            spec->members[pos] = spec->members[pos - 1];
            pos--;
        }

        spec->member_names[pos] = name;
        spec->members[pos].type = type;
        spec->members[pos].offset = size;

        size += member_size;
    }

    spec->size  = (size + align - 1) / align * align;
    spec->align = align;

    st->REPR_data = spec;
}

static const MVMREPROps this_repr = {
    type_object_for,
    allocate,
    initialize,
    NULL, /* copy_to */
    { /* attr_funcs */
        get_attribute,
        bind_attribute,
        hint_for,
        is_attribute_initialized
    },
    MVM_REPR_DEFAULT_BOX_FUNCS,
    MVM_REPR_DEFAULT_POS_FUNCS,
    MVM_REPR_DEFAULT_ASS_FUNCS,
    MVM_REPR_DEFAULT_ELEMS,
    get_storage_spec,
    NULL, /* change_type */
    NULL, /* serialize */
    NULL, /* deserialize */
    NULL, /* serialize_repr_data */
    NULL, /* deserialize_repr_data */
    NULL, /* deserialize_stable_size */
    gc_mark,
    NULL, /* gc_free */
    NULL, /* gc_cleanup */
    gc_mark_repr_data,
    gc_free_repr_data,
    compose,
    "CStruct",
    MVM_REPR_ID_CStruct,
    0, /* refs_frames */
};
