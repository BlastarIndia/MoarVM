#include "moar.h"

/* This representation's function pointer table. */
static const MVMREPROps this_repr;

/* Creates a new type object of this representation, and associates it with
 * the given HOW. */
static MVMObject * type_object_for(MVMThreadContext *tc, MVMObject *HOW) {
    MVMSTable *st  = MVM_gc_allocate_stable(tc, &this_repr, HOW);

    MVMROOT(tc, st, {
        MVMObject *obj = MVM_gc_allocate_type_object(tc, st);
        MVM_ASSIGN_REF(tc, st, st->WHAT, obj);
        st->size = sizeof(MVMP6int);
    });

    return st->WHAT;
}

/* Creates a new instance based on the type object. */
static MVMObject * allocate(MVMThreadContext *tc, MVMSTable *st) {
    return MVM_gc_allocate_object(tc, st);
}

/* Copies the body of one object to another. */
static void copy_to(MVMThreadContext *tc, MVMSTable *st, void *src, MVMObject *dest_root, void *dest) {
    MVMP6intBody *src_body  = (MVMP6intBody *)src;
    MVMP6intBody *dest_body = (MVMP6intBody *)dest;
    dest_body->value = src_body->value;
}

static void set_int(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data, MVMint64 value) {
    ((MVMP6intBody *)data)->value = value;
}

static MVMint64 get_int(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data) {
    return ((MVMP6intBody *)data)->value;
}

/* Gets the storage specification for this representation. */
static MVMStorageSpec get_storage_spec(MVMThreadContext *tc, MVMSTable *st) {
    MVMStorageSpec spec;
    spec.inlineable      = MVM_STORAGE_SPEC_INLINED;
    spec.bits            = sizeof(MVMint64) * 8;
    spec.boxed_primitive = MVM_STORAGE_SPEC_BP_INT;
    spec.can_box         = MVM_STORAGE_SPEC_CAN_BOX_INT;
    return spec;
}

/* Compose the representation. */
static void compose(MVMThreadContext *tc, MVMSTable *st, MVMObject *info) {
    /* XXX size conveyed here */
}

/* Set the size of the STable. */
static void deserialize_stable_size(MVMThreadContext *tc, MVMSTable *st, MVMSerializationReader *reader) {
    st->size = sizeof(MVMP6int);
}

static void deserialize(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data, MVMSerializationReader *reader) {
    ((MVMP6intBody *)data)->value = reader->read_int(tc, reader);
}

static void serialize(MVMThreadContext *tc, MVMSTable *st, void *data, MVMSerializationWriter *writer) {
    writer->write_int(tc, writer, ((MVMP6intBody *)data)->value);
}

/* Initializes the representation. */
const MVMREPROps * MVMP6int_initialize(MVMThreadContext *tc) {
    return &this_repr;
}

static const MVMREPROps this_repr = {
    type_object_for,
    allocate,
    NULL, /* initialize */
    copy_to,
    MVM_REPR_DEFAULT_ATTR_FUNCS,
    {
        set_int,
        get_int,
        MVM_REPR_DEFAULT_SET_NUM,
        MVM_REPR_DEFAULT_GET_NUM,
        MVM_REPR_DEFAULT_SET_STR,
        MVM_REPR_DEFAULT_GET_STR,
        MVM_REPR_DEFAULT_GET_BOXED_REF
    },    /* box_funcs */
    MVM_REPR_DEFAULT_POS_FUNCS,
    MVM_REPR_DEFAULT_ASS_FUNCS,
    MVM_REPR_DEFAULT_ELEMS,
    get_storage_spec,
    NULL, /* change_type */
    serialize,
    deserialize, /* deserialize */
    NULL, /* serialize_repr_data */
    NULL, /* deserialize_repr_data */
    deserialize_stable_size,
    NULL, /* gc_mark */
    NULL, /* gc_free */
    NULL, /* gc_cleanup */
    NULL, /* gc_mark_repr_data */
    NULL, /* gc_free_repr_data */
    compose,
    "P6int", /* name */
    MVM_REPR_ID_P6int,
    0, /* refs_frames */
};
