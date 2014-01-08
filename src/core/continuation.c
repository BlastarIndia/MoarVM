#include "moar.h"

static MVMCallsite      no_arg_callsite = { NULL, 0, 0, 0 };
static MVMCallsiteEntry obj_arg_flags[] = { MVM_CALLSITE_ARG_OBJ };
static MVMCallsite     obj_arg_callsite = { obj_arg_flags, 1, 1, 0 };

static void clear_tag(MVMThreadContext *tc, void *sr_data) {
    MVMContinuationTag **update = &tc->cur_frame->continuation_tags;
    while (*update) {
        if (*update == sr_data) {
            *update = (*update)->next;
            free(sr_data);
            return;
        }
        update = &((*update)->next);
    }
    MVM_exception_throw_adhoc(tc, "Internal error: failed to clear continuation tag");
}
void MVM_continuation_reset(MVMThreadContext *tc, MVMObject *tag, 
                            MVMObject *code, MVMRegister *res_reg) {
    /* Save the tag. */
    MVMContinuationTag *tag_record = malloc(sizeof(MVMContinuationTag));
    tag_record->tag = tag;
    tag_record->next = tc->cur_frame->continuation_tags;
    tc->cur_frame->continuation_tags = tag_record;

    /* Run the passed code. */
    code = MVM_frame_find_invokee(tc, code, NULL);
    MVM_args_setup_thunk(tc, res_reg, MVM_RETURN_OBJ, &no_arg_callsite);
    tc->cur_frame->special_return = clear_tag;
    tc->cur_frame->special_return_data = tag_record;
    STABLE(code)->invoke(tc, code, &no_arg_callsite, tc->cur_frame->args);
}

void MVM_continuation_control(MVMThreadContext *tc, MVMint64 protect,
                              MVMObject *tag, MVMObject *code,
                              MVMRegister *res_reg) {
    MVMObject *cont;

    /* Hunt the tag on the stack. */
    MVMFrame           *root_frame  = tc->cur_frame;
    MVMContinuationTag *tag_record  = NULL;
    while (root_frame) {
        tag_record = root_frame->continuation_tags;
        while (tag_record) {
            if (!tag || tag_record->tag == tag)
                break;
            tag_record = tag_record->next;
        }
        if (tag_record)
            break;
        root_frame = root_frame->caller;
    }
    if (!tag_record)
        MVM_exception_throw_adhoc(tc, "No matching continuation reset found");

    /* Create continuation. */
    MVMROOT(tc, code, {
        cont = MVM_repr_alloc_init(tc, tc->instance->boot_types.BOOTContinuation);
        ((MVMContinuation *)cont)->body.top     = tc->cur_frame;
        ((MVMContinuation *)cont)->body.addr    = *tc->interp_cur_op;
        ((MVMContinuation *)cont)->body.res_reg = res_reg;
    });

    /* Move back to the root frame. */
    tc->cur_frame = root_frame;
    *(tc->interp_cur_op) = tc->cur_frame->return_address;
    *(tc->interp_bytecode_start) = tc->cur_frame->static_info->body.bytecode;
    *(tc->interp_reg_base) = tc->cur_frame->work;
    *(tc->interp_cu) = tc->cur_frame->static_info->body.cu;

    /* Clear special return handler, given we didn't just fall out of the
     * reset. */
    tc->cur_frame->special_return = NULL;
    tc->cur_frame->special_return_data = NULL;

    /* If we're not protecting the follow-up call, remove the tag record. */
    if (!protect)
        clear_tag(tc, tag_record);

    /* Invoke specified code, passing the continuation. We return to
     * interpreter to run this, which then returns control to the
     * original reset or invoke. */
    code = MVM_frame_find_invokee(tc, code, NULL);
    MVM_args_setup_thunk(tc, tc->cur_frame->return_value, tc->cur_frame->return_type, &obj_arg_callsite);
    tc->cur_frame->args[0].o = cont;
    STABLE(code)->invoke(tc, code, &obj_arg_callsite, tc->cur_frame->args);
}