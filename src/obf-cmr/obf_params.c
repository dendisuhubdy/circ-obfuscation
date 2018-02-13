#include "obf_params.h"
#include "obfuscator.h"
#include "../mife-cmr/mife.h"
#include "../util.h"

size_t
obf_cmr_num_encodings(const acirc_t *circ)
{
    size_t count = 0;
    for (size_t i = 0; i < acirc_nslots(circ); ++i)
        count += acirc_symnum(circ, i) * mife_num_encodings_encrypt(circ, i);
    return count;
}

static void
obf_cmr_op_free(obf_params_t *op)
{
    if (op)
        free(op);
}

static obf_params_t *
obf_cmr_op_new(const acirc_t *circ, void *vparams)
{
    (void) circ;
    const obf_cmr_params_t *params = vparams;
    obf_params_t *op;

    op = xcalloc(1, sizeof op[0]);
    op->npowers = params->npowers;
    return op;
}

static void
obf_cmr_op_print(const obf_params_t *op, const acirc_t *circ)
{
    const size_t nconsts = acirc_nconsts(circ);
    const size_t noutputs = acirc_noutputs(circ);
    const size_t has_consts = nconsts ? 1 : 0;
    size_t nencodings;
    nencodings = obf_cmr_num_encodings(circ)                \
        + noutputs                                          \
        + acirc_nslots(circ) * op->npowers                  \
        + (has_consts ? acirc_symlen(circ, acirc_nsymbols(circ)) : 1);
    fprintf(stderr, "Obfuscation parameters:\n");
    fprintf(stderr, "———— # powers: .. %lu\n", op->npowers);
    fprintf(stderr, "———— # encodings: %lu\n", nencodings);
}

op_vtable obf_cmr_op_vtable =
{
    .new = obf_cmr_op_new,
    .free = obf_cmr_op_free,
    .print = obf_cmr_op_print,
};
