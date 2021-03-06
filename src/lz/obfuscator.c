#include "obfuscator.h"
#include "obf_params.h"
#include "vtables.h"
#include "reflist.h"
#include "util.h"

#include <assert.h>
#include <string.h>
#include <threadpool.h>

typedef struct obfuscation {
    const mmap_vtable *mmap;
    const pp_vtable *pp_vt;
    const sp_vtable *sp_vt;
    const encoding_vtable *enc_vt;
    const obf_params_t *op;
    secret_params *sp;
    public_params *pp;
    encoding ****shat;          // [c][Σ][ℓ]
    encoding ****uhat;          // [c][Σ][npowers]
    encoding ****zhat;          // [c][Σ][γ]
    encoding ****what;          // [c][Σ][γ]
    encoding **yhat;            // [m]
    encoding **vhat;            // [npowers]
    encoding **Chatstar;        // [γ]
} obfuscation;

typedef struct work_args {
    const mmap_vtable *mmap;
    acircref ref;
    const acirc *c;
    const int *inputs;
    const obfuscation *obf;
    bool *mine;
    int *ready;
    void *cache;
    ref_list *deps;
    threadpool *pool;
    unsigned int *kappas;
    int *rop;
} work_args;

typedef struct obf_args {
    const encoding_vtable *vt;
    encoding *enc;
    mpz_t inps[2];
    index_set *ix;
    const secret_params *sp;
    pthread_mutex_t *count_lock;
    size_t *count;
    size_t total;
} obf_args;

static void obf_worker(void *wargs)
{
    obf_args *const args = wargs;

    encode(args->vt, args->enc, args->inps, 2, args->ix, args->sp);
    if (g_verbose) {
        pthread_mutex_lock(args->count_lock);
        print_progress(++*args->count, args->total);
        pthread_mutex_unlock(args->count_lock);
    }
    mpz_vect_clear(args->inps, 2);
    index_set_free(args->ix);
    free(args);
}

static void
__encode(threadpool *pool, const encoding_vtable *vt, encoding *enc, mpz_t inps[2],
         index_set *ix, const secret_params *sp, pthread_mutex_t *count_lock,
         size_t *count, size_t total)
{
    obf_args *args = my_calloc(1, sizeof args[0]);
    args->vt = vt;
    args->enc = enc;
    mpz_vect_init(args->inps, 2);
    mpz_set(args->inps[0], inps[0]);
    mpz_set(args->inps[1], inps[1]);
    args->ix = ix;
    args->sp = sp;
    args->count_lock = count_lock;
    args->count = count;
    args->total = total;
    threadpool_add_job(pool, obf_worker, args);
}

static obfuscation *
_alloc(const mmap_vtable *mmap, const obf_params_t *op)
{
    obfuscation *obf;

    const circ_params_t *cp = &op->cp;
    const size_t nconsts = cp->circ->consts.n;
    const size_t has_consts = nconsts ? 1 : 0;
    const size_t ninputs = cp->n - has_consts;
    const size_t noutputs = cp->m;

    obf = my_calloc(1, sizeof obf[0]);
    obf->mmap = mmap;
    obf->enc_vt = get_encoding_vtable(mmap);
    obf->pp_vt = get_pp_vtable(mmap);
    obf->sp_vt = get_sp_vtable(mmap);
    obf->op = op;
    obf->shat = my_calloc(ninputs, sizeof obf->shat[0]);
    obf->uhat = my_calloc(ninputs, sizeof obf->uhat[0]);
    obf->zhat = my_calloc(ninputs, sizeof obf->zhat[0]);
    obf->what = my_calloc(ninputs, sizeof obf->what[0]);
    for (size_t k = 0; k < ninputs; k++) {
        obf->shat[k] = my_calloc(cp->qs[k], sizeof obf->shat[0][0]);
        obf->uhat[k] = my_calloc(cp->qs[k], sizeof obf->uhat[0][0]);
        obf->zhat[k] = my_calloc(cp->qs[k], sizeof obf->zhat[0][0]);
        obf->what[k] = my_calloc(cp->qs[k], sizeof obf->what[0][0]);
        for (size_t s = 0; s < cp->qs[k]; s++) {
            obf->shat[k][s] = my_calloc(cp->ds[k], sizeof obf->shat[0][0][0]);
            obf->uhat[k][s] = my_calloc(op->npowers, sizeof obf->uhat[0][0][0]);
            obf->zhat[k][s] = my_calloc(noutputs, sizeof obf->zhat[0][0][0]);
            obf->what[k][s] = my_calloc(noutputs, sizeof obf->what[0][0][0]);
        }
    }
    obf->yhat = my_calloc(nconsts, sizeof obf->yhat[0]);
    obf->vhat = my_calloc(op->npowers, sizeof obf->vhat[0]);
    obf->Chatstar = my_calloc(noutputs, sizeof obf->Chatstar[0]);

    return obf;
}

static void
_free(obfuscation *obf)
{
    if (obf == NULL)
        return;

    const obf_params_t *op = obf->op;
    const circ_params_t *cp = &op->cp;
    const size_t nconsts = cp->circ->consts.n;
    const size_t has_consts = nconsts ? 1 : 0;
    const size_t ninputs = cp->n - has_consts;
    const size_t noutputs = cp->m;

    for (size_t k = 0; k < ninputs; k++) {
        for (size_t s = 0; s < cp->qs[k]; s++) {
            for (size_t j = 0; j < cp->ds[k]; j++)
                encoding_free(obf->enc_vt, obf->shat[k][s][j]);
            for (size_t p = 0; p < op->npowers; p++)
                encoding_free(obf->enc_vt, obf->uhat[k][s][p]);
            for (size_t o = 0; o < noutputs; o++) {
                encoding_free(obf->enc_vt, obf->zhat[k][s][o]);
                encoding_free(obf->enc_vt, obf->what[k][s][o]);
            }
            free(obf->uhat[k][s]);
            free(obf->shat[k][s]);
            free(obf->zhat[k][s]);
            free(obf->what[k][s]);
        }
        free(obf->shat[k]);
        free(obf->uhat[k]);
        free(obf->zhat[k]);
        free(obf->what[k]);
    }
    free(obf->shat);
    free(obf->uhat);
    free(obf->zhat);
    free(obf->what);
    for (size_t i = 0; i < nconsts; i++)
        encoding_free(obf->enc_vt, obf->yhat[i]);
    free(obf->yhat);
    for (size_t p = 0; p < op->npowers; p++)
        encoding_free(obf->enc_vt, obf->vhat[p]);
    free(obf->vhat);
    for (size_t i = 0; i < noutputs; i++)
        encoding_free(obf->enc_vt, obf->Chatstar[i]);
    free(obf->Chatstar);

    if (obf->pp)
        public_params_free(obf->pp_vt, obf->pp);
    if (obf->sp)
        secret_params_free(obf->sp_vt, obf->sp);

    free(obf);
}

static obfuscation *
_obfuscate(const mmap_vtable *mmap, const obf_params_t *op, size_t secparam,
           size_t *kappa, size_t nthreads, aes_randstate_t rng)
{
    obfuscation *obf;

    const circ_params_t *cp = &op->cp;
    const size_t nconsts = cp->circ->consts.n;
    const size_t has_consts = nconsts ? 1 : 0;
    const size_t ninputs = cp->n - has_consts;
    const size_t noutputs = cp->m;

    if (op->npowers == 0 || secparam == 0)
        return NULL;

    obf = _alloc(mmap, op);
    obf->sp = secret_params_new(obf->sp_vt, op, secparam, kappa, nthreads, rng);
    if (obf->sp == NULL) {
        _free(obf);
        return NULL;
    }
    obf->pp = public_params_new(obf->pp_vt, obf->sp_vt, obf->sp);
    if (obf->pp == NULL) {
        _free(obf);
        return NULL;
    }

    for (size_t k = 0; k < ninputs; k++) {
        for (size_t s = 0; s < cp->qs[k]; s++) {
            for (size_t j = 0; j < cp->ds[k]; j++) {
                obf->shat[k][s][j] = encoding_new(obf->enc_vt, obf->pp_vt, obf->pp);
            }
            for (size_t p = 0; p < op->npowers; p++) {
                obf->uhat[k][s][p] = encoding_new(obf->enc_vt, obf->pp_vt, obf->pp);
            }
            for (size_t o = 0; o < noutputs; o++) {
                obf->zhat[k][s][o] = encoding_new(obf->enc_vt, obf->pp_vt, obf->pp);
                obf->what[k][s][o] = encoding_new(obf->enc_vt, obf->pp_vt, obf->pp);
            }
        }
    }
    for (size_t i = 0; i < nconsts; i++) {
        obf->yhat[i] = encoding_new(obf->enc_vt, obf->pp_vt, obf->pp);
    }
    for (size_t p = 0; p < op->npowers; p++) {
        obf->vhat[p] = encoding_new(obf->enc_vt, obf->pp_vt, obf->pp);
    }
    for (size_t i = 0; i < noutputs; i++) {
        obf->Chatstar[i] = encoding_new(obf->enc_vt, obf->pp_vt, obf->pp);
    }
    
    acirc *const circ = cp->circ;
    mpz_t *const moduli =
        mpz_vect_create_of_fmpz(obf->mmap->sk->plaintext_fields(obf->sp->sk),
                                obf->mmap->sk->nslots(obf->sp->sk));
    index_set *const ix = index_set_new(obf_params_nzs(cp));

    const size_t ell = array_max(cp->ds, ninputs);
    const size_t q = array_max(cp->qs, ninputs);

    mpz_t inps[2];
    mpz_t alpha[ninputs * ell];
    mpz_t beta[nconsts];
    mpz_t gamma[ninputs][q][noutputs];
    mpz_t delta[ninputs][q][noutputs];
    mpz_t Cstar[noutputs];
    threadpool *pool = threadpool_create(nthreads);

    pthread_mutex_t count_lock;
    size_t count = 0;
    const size_t total = obf_params_num_encodings(op);

    pthread_mutex_init(&count_lock, NULL);
    mpz_vect_init(inps, 2);

    assert(obf->mmap->sk->nslots(obf->sp->sk) >= 2);

    for (size_t k = 0; k < ninputs; k++) {
        for (size_t j = 0; j < cp->ds[k]; j++) {
            mpz_init(alpha[k * cp->ds[k] + j]);
            mpz_randomm_inv(alpha[k * cp->ds[k] + j], rng, moduli[1]);
        }
    }
    for (size_t k = 0; k < ninputs; k++) {
        for (size_t s = 0; s < cp->qs[k]; s++) {
            for (size_t o = 0; o < noutputs; o++) {
                mpz_inits(gamma[k][s][o], delta[k][s][o], NULL);
                mpz_randomm_inv(delta[k][s][o], rng, moduli[0]);
                mpz_randomm_inv(gamma[k][s][o], rng, moduli[1]);
            }
        }
    }
    for (size_t i = 0; i < nconsts; i++) {
        mpz_init(beta[i]);
        mpz_randomm_inv(beta[i], rng, moduli[1]);
    }

    for (size_t o = 0; o < noutputs; o++) {
        mpz_init(Cstar[o]);
        acirc_eval_mpz_mod(Cstar[o], circ, circ->outputs.buf[o], alpha, beta, moduli[1]);
    }

    unsigned long const_deg[noutputs];
    unsigned long const_deg_max = 0;
    unsigned long var_deg[ninputs][noutputs];
    unsigned long var_deg_max[ninputs];
    acirc_memo *memo;

    memset(const_deg, '\0', sizeof const_deg);
    memset(var_deg, '\0', sizeof var_deg);
    memset(var_deg_max, '\0', sizeof var_deg_max);

    memo = acirc_memo_new(circ);
    for (size_t o = 0; o < noutputs; o++) {
        const_deg[o] = acirc_const_degree(circ, circ->outputs.buf[o], memo);
        if (const_deg[o] > const_deg_max)
            const_deg_max = const_deg[o];
    }
    acirc_memo_free(memo, circ);

    memo = acirc_memo_new(circ);
    for (size_t k = 0; k < ninputs; k++) {
        for (size_t o = 0; o < noutputs; o++) {
            var_deg[k][o] = acirc_var_degree(circ, circ->outputs.buf[o], k, memo);
            if (var_deg[k][o] > var_deg_max[k])
                var_deg_max[k] = var_deg[k][o];
        }
    }
    acirc_memo_free(memo, circ);

    if (g_verbose)
        print_progress(count, total);

    for (size_t k = 0; k < ninputs; k++) {
        for (size_t s = 0; s < cp->qs[k]; s++) {
            for (size_t j = 0; j < cp->ds[k]; j++) {
                mpz_set_ui(inps[0], op->sigma ? s == j : bit(s, j));
                mpz_set   (inps[1], alpha[k * cp->ds[k] + j]);
                index_set_clear(ix);
                ix_s_set(ix, cp, k, s, 1);
                __encode(pool, obf->enc_vt, obf->shat[k][s][j], inps,
                         index_set_copy(ix), obf->sp, &count_lock, &count,
                         total);
            }
            mpz_set_ui(inps[0], 1);
            mpz_set_ui(inps[1], 1);
            index_set_clear(ix);
            for (size_t p = 0; p < op->npowers; p++) {
                ix_s_set(ix, cp, k, s, 1 << p);
                __encode(pool, obf->enc_vt, obf->uhat[k][s][p], inps,
                         index_set_copy(ix), obf->sp, &count_lock, &count,
                         total);
            }
            for (size_t o = 0; o < noutputs; o++) {
                index_set_clear(ix);
                if (k == 0)
                    ix_y_set(ix, cp, const_deg_max - const_deg[o]);
                for (size_t r = 0; r < cp->qs[k]; r++)
                    ix_s_set(ix, cp, k, r, r == s ? var_deg_max[k] - var_deg[k][o] : var_deg_max[k]);
                ix_z_set(ix, cp, k, 1);
                ix_w_set(ix, cp, k, 1);
                mpz_set(inps[0], delta[k][s][o]);
                mpz_set(inps[1], gamma[k][s][o]);
                __encode(pool, obf->enc_vt, obf->zhat[k][s][o], inps,
                         index_set_copy(ix), obf->sp, &count_lock, &count,
                         total);

                index_set_clear(ix);
                ix_w_set(ix, cp, k, 1);
                mpz_set_ui(inps[0], 0);
                mpz_set   (inps[1], gamma[k][s][o]);
                __encode(pool, obf->enc_vt, obf->what[k][s][o], inps,
                         index_set_copy(ix), obf->sp, &count_lock, &count,
                         total);
            }
        }
    }

    for (size_t i = 0; i < nconsts; i++) {
        index_set_clear(ix);
        ix_y_set(ix, cp, 1);
        mpz_set_si(inps[0], circ->consts.buf[i]);
        mpz_set   (inps[1], beta[i]);
        __encode(pool, obf->enc_vt, obf->yhat[i], inps, index_set_copy(ix),
                 obf->sp, &count_lock, &count, total);
    }
    for (size_t p = 0; p < op->npowers; p++) {
        index_set_clear(ix);
        ix_y_set(ix, cp, 1 << p);
        mpz_set_ui(inps[0], 1);
        mpz_set_ui(inps[1], 1);
        __encode(pool, obf->enc_vt, obf->vhat[p], inps, index_set_copy(ix),
                 obf->sp, &count_lock, &count, total);
    }

    for (size_t i = 0; i < noutputs; i++) {
        index_set_clear(ix);
        ix_y_set(ix, cp, const_deg_max);
        for (size_t k = 0; k < ninputs; k++) {
            for (size_t s = 0; s < cp->qs[k]; s++) {
                ix_s_set(ix, cp, k, s, var_deg_max[k]);
            }
            ix_z_set(ix, cp, k, 1);
        }

        mpz_set_ui(inps[0], 0);
        mpz_set   (inps[1], Cstar[i]);

        __encode(pool, obf->enc_vt, obf->Chatstar[i], inps,
                 index_set_copy(ix), obf->sp, &count_lock, &count, total);
    }

    threadpool_destroy(pool);
    pthread_mutex_destroy(&count_lock);

    index_set_free(ix);
    mpz_vect_clear(inps, 2);

    for (size_t k = 0; k < ninputs; k++) {
        for (size_t j = 0; j < cp->ds[k]; j++) {
            mpz_clear(alpha[k * cp->ds[k] + j]);
        }
    }

    for (size_t k = 0; k < ninputs; k++) {
        for (size_t s = 0; s < cp->qs[k]; s++) {
            for (size_t o = 0; o < noutputs; o++) {
                mpz_clears(delta[k][s][o], gamma[k][s][o], NULL);
            }
        }
    }
    for (size_t i = 0; i < nconsts; i++)
        mpz_clear(beta[i]);
    for (size_t i = 0; i < noutputs; i++)
        mpz_clear(Cstar[i]);

    mpz_vect_free(moduli, obf->mmap->sk->nslots(obf->sp->sk));

    return obf;
}

static int
_fwrite(const obfuscation *const obf, FILE *const fp)
{
    const obf_params_t *const op = obf->op;

    const circ_params_t *cp = &op->cp;
    const size_t nconsts = cp->circ->consts.n;
    const size_t has_consts = nconsts ? 1 : 0;
    const size_t ninputs = cp->n - has_consts;
    const size_t noutputs = cp->m;

    public_params_fwrite(obf->pp_vt, obf->pp, fp);
    for (size_t k = 0; k < ninputs; k++) {
        for (size_t s = 0; s < cp->qs[k]; s++) {
            for (size_t j = 0; j < cp->ds[k]; j++)
                encoding_fwrite(obf->enc_vt, obf->shat[k][s][j], fp);
            for (size_t p = 0; p < op->npowers; p++)
                encoding_fwrite(obf->enc_vt, obf->uhat[k][s][p], fp);
            for (size_t o = 0; o < noutputs; o++) {
                encoding_fwrite(obf->enc_vt, obf->zhat[k][s][o], fp);
                encoding_fwrite(obf->enc_vt, obf->what[k][s][o], fp);
            }
        }
    }
    for (size_t j = 0; j < nconsts; j++)
        encoding_fwrite(obf->enc_vt, obf->yhat[j], fp);
    for (size_t p = 0; p < op->npowers; p++)
        encoding_fwrite(obf->enc_vt, obf->vhat[p], fp);
    for (size_t k = 0; k < noutputs; k++)
        encoding_fwrite(obf->enc_vt, obf->Chatstar[k], fp);
    return OK;
}

static obfuscation *
_fread(const mmap_vtable *mmap, const obf_params_t *op, FILE *fp)
{
    obfuscation *obf;

    const circ_params_t *cp = &op->cp;
    const size_t nconsts = cp->circ->consts.n;
    const size_t has_consts = nconsts ? 1 : 0;
    const size_t ninputs = cp->n - has_consts;
    const size_t noutputs = cp->m;

    if ((obf = _alloc(mmap, op)) == NULL)
        return NULL;

    obf->pp = public_params_fread(obf->pp_vt, op, fp);
    for (size_t k = 0; k < ninputs; k++) {
        for (size_t s = 0; s < cp->qs[k]; s++) {
            for (size_t j = 0; j < cp->ds[k]; j++)
                obf->shat[k][s][j] = encoding_fread(obf->enc_vt, fp);
            for (size_t p = 0; p < op->npowers; p++)
                obf->uhat[k][s][p] = encoding_fread(obf->enc_vt, fp);
            for (size_t o = 0; o < noutputs; o++) {
                obf->zhat[k][s][o] = encoding_fread(obf->enc_vt, fp);
                obf->what[k][s][o] = encoding_fread(obf->enc_vt, fp);
            }
        }
    }
    for (size_t i = 0; i < nconsts; i++)
        obf->yhat[i] = encoding_fread(obf->enc_vt, fp);
    for (size_t p = 0; p < op->npowers; p++)
        obf->vhat[p] = encoding_fread(obf->enc_vt, fp);
    for (size_t i = 0; i < noutputs; i++)
        obf->Chatstar[i] = encoding_fread(obf->enc_vt, fp);
    return obf;
}

static size_t g_max_npowers = 0;

static void _raise_encoding(const obfuscation *obf, encoding *x, encoding **ys, size_t diff)
{
    while (diff > 0) {
        // want to find the largest power we obfuscated to multiply by
        size_t p = 0;
        while (((size_t) 1 << (p+1)) <= diff && (p+1) < obf->op->npowers)
            p++;
        if (g_max_npowers < p + 1)
            g_max_npowers = p + 1;
        encoding_mul(obf->enc_vt, obf->pp_vt, x, x, ys[p], obf->pp);
        diff -= (1 << p);
    }
}

static void raise_encoding(const obfuscation *obf, encoding *x, const index_set *target)
{
    const circ_params_t *cp = &obf->op->cp;
    const size_t ninputs = cp->n - (cp->circ->consts.n ? 1 : 0);

    index_set *const ix =
        index_set_difference(target, obf->enc_vt->mmap_set(x));
    size_t diff;
    for (size_t k = 0; k < ninputs; k++) {
        for (size_t s = 0; s < cp->qs[k]; s++) {
            diff = ix_s_get(ix, cp, k, s);
            _raise_encoding(obf, x, obf->uhat[k][s], diff);
        }
    }
    diff = ix_y_get(ix, cp);
    _raise_encoding(obf, x, obf->vhat, diff);
    index_set_free(ix);
}

static void
raise_encodings(const obfuscation *obf, encoding *x, encoding *y)
{
    index_set *const ix = index_set_union(obf->enc_vt->mmap_set(x),
                                          obf->enc_vt->mmap_set(y));
    raise_encoding(obf, x, ix);
    raise_encoding(obf, y, ix);
    index_set_free(ix);
}

static void eval_worker(void *vargs)
{
    work_args *const wargs = vargs;
    const acircref ref = wargs->ref;
    const acirc *const c = wargs->c;
    const int *const inputs = wargs->inputs;
    const obfuscation *const obf = wargs->obf;
    bool *const mine = wargs->mine;
    int *const ready = wargs->ready;
    encoding **cache = wargs->cache;
    ref_list *const deps = wargs->deps;
    threadpool *const pool = wargs->pool;
    unsigned int *const kappas = wargs->kappas;
    int *const rop = wargs->rop;

    const acirc_operation op = c->gates.gates[ref].op;
    const acircref *const args = c->gates.gates[ref].args;
    encoding *res;

    const circ_params_t *cp = &obf->op->cp;
    const size_t has_consts = cp->circ->consts.n ? 1 : 0;
    const size_t ninputs = cp->n - has_consts;
    const size_t noutputs = cp->m;

    switch (op) {
    case OP_INPUT: {
        const size_t id = args[0];
        const sym_id sym = obf->op->chunker(id, c->ninputs, ninputs);
        const size_t k = sym.sym_number;
        const size_t s = inputs[k];
        const size_t j = sym.bit_number;
        res = obf->shat[k][s][j];
        mine[ref] = false;
        break;
    }
    case OP_CONST: {
        const size_t i = args[0];
        res = obf->yhat[i];
        mine[ref] = false;
        break;
    }
    case OP_ADD: case OP_SUB: case OP_MUL: {
        assert(c->gates.gates[ref].nargs == 2);
        res = encoding_new(obf->enc_vt, obf->pp_vt, obf->pp);
        mine[ref] = true;

        const encoding *const x = cache[args[0]];
        const encoding *const y = cache[args[1]];

        if (op == OP_MUL) {
            encoding_mul(obf->enc_vt, obf->pp_vt, res, x, y, obf->pp);
        } else {
            encoding *tmp_x, *tmp_y;
            tmp_x = encoding_new(obf->enc_vt, obf->pp_vt, obf->pp);
            tmp_y = encoding_new(obf->enc_vt, obf->pp_vt, obf->pp);
            encoding_set(obf->enc_vt, tmp_x, x);
            encoding_set(obf->enc_vt, tmp_y, y);
            if (!index_set_eq(obf->enc_vt->mmap_set(tmp_x), obf->enc_vt->mmap_set(tmp_y)))
                raise_encodings(obf, tmp_x, tmp_y);
            if (op == OP_ADD) {
                encoding_add(obf->enc_vt, obf->pp_vt, res, tmp_x, tmp_y, obf->pp);
            } else if (op == OP_SUB) {
                encoding_sub(obf->enc_vt, obf->pp_vt, res, tmp_x, tmp_y, obf->pp);
            } else {
                abort();
            }
            encoding_free(obf->enc_vt, tmp_x);
            encoding_free(obf->enc_vt, tmp_y);
        }
        break;
    }
    case OP_SET:
        res = encoding_new(obf->enc_vt, obf->pp_vt, obf->pp);
        mine[ref] = true;
        encoding_set(obf->enc_vt, res, cache[args[0]]);
        break;
    default:
        fprintf(stderr, "fatal: op not supported\n");
        abort();
    }        

    cache[ref] = res;

    ref_list_node *node = &deps->refs[ref];
    for (size_t i = 0; i < node->cur; ++i) {
        const int num = __sync_add_and_fetch(&ready[node->refs[i]], 1);
        if (num == 2) {
            work_args *newargs = my_calloc(1, sizeof newargs[0]);
            memcpy(newargs, wargs, sizeof newargs[0]);
            newargs->ref = node->refs[i];
            threadpool_add_job(pool, eval_worker, newargs);
        }
    }

    free(wargs);

    // addendum: is this ref an output bit? if so, we should zero test it.
    ssize_t output = -1;
    for (size_t i = 0; i < noutputs; i++) {
        if (ref == c->outputs.buf[i]) {
            output = i;
            break;
        }
    }

    if (output != -1) {
        encoding *out, *lhs, *rhs, *tmp;
        const index_set *const toplevel = obf->pp_vt->toplevel(obf->pp);

        out = encoding_new(obf->enc_vt, obf->pp_vt, obf->pp);
        lhs = encoding_new(obf->enc_vt, obf->pp_vt, obf->pp);
        rhs = encoding_new(obf->enc_vt, obf->pp_vt, obf->pp);
        tmp = encoding_new(obf->enc_vt, obf->pp_vt, obf->pp);

        /* Compute LHS */
        encoding_set(obf->enc_vt, lhs, res);
        for (size_t k = 0; k < ninputs; k++) {
            encoding_set(obf->enc_vt, tmp, lhs);
            encoding_mul(obf->enc_vt, obf->pp_vt, lhs, tmp,
                         obf->zhat[k][inputs[k]][output], obf->pp);
        }
        raise_encoding(obf, lhs, toplevel);
        if (!index_set_eq(obf->enc_vt->mmap_set(lhs), toplevel)) {
            fprintf(stderr, "lhs != toplevel\n");
            index_set_print(obf->enc_vt->mmap_set(lhs));
            index_set_print(toplevel);
            rop[output] = 1;
            goto cleanup;
        }

        /* Compute RHS */
        encoding_set(obf->enc_vt, rhs, obf->Chatstar[output]);
        for (size_t k = 0; k < ninputs; k++) {
            encoding_set(obf->enc_vt, tmp, rhs);
            encoding_mul(obf->enc_vt, obf->pp_vt, rhs, tmp,
                         obf->what[k][inputs[k]][output], obf->pp);
        }
        if (!index_set_eq(obf->enc_vt->mmap_set(rhs), toplevel)) {
            fprintf(stderr, "rhs != toplevel\n");
            index_set_print(obf->enc_vt->mmap_set(rhs));
            index_set_print(toplevel);
            rop[output] = 1;
            goto cleanup;
        }
        encoding_sub(obf->enc_vt, obf->pp_vt, out, lhs, rhs, obf->pp);
        rop[output] = !encoding_is_zero(obf->enc_vt, obf->pp_vt, out, obf->pp);
        if (kappas)
            kappas[output] = encoding_get_degree(obf->enc_vt, out);

    cleanup:
        encoding_free(obf->enc_vt, out);
        encoding_free(obf->enc_vt, lhs);
        encoding_free(obf->enc_vt, rhs);
        encoding_free(obf->enc_vt, tmp);
    }
}


static int
_evaluate(const obfuscation *obf, int *outputs, size_t noutputs,
          const int *inputs, size_t ninputs, size_t nthreads,
          size_t *kappa, size_t *npowers)
{
    const circ_params_t *cp = &obf->op->cp;
    const acirc *const c = cp->circ;
    const size_t has_consts = cp->circ->consts.n ? 1 : 0;
    const size_t ell = array_max(cp->ds, cp->n - has_consts);
    const size_t q = array_max(cp->qs, cp->n - has_consts);
    int ret = ERR;

    if (ninputs != c->ninputs) {
        fprintf(stderr, "error: obf evaluate: invalid number of inputs\n");
        return ERR;
    } else if (noutputs != cp->m) {
        fprintf(stderr, "error: obf evaluate: invalid number of outputs\n");
        return ERR;
    }

    encoding **cache = my_calloc(acirc_nrefs(c), sizeof cache[0]);
    bool *mine = my_calloc(acirc_nrefs(c), sizeof mine[0]);
    int *ready = my_calloc(acirc_nrefs(c), sizeof ready[0]);
    unsigned int *kappas = my_calloc(c->outputs.n, sizeof kappas[0]);
    int *input_syms = get_input_syms(inputs, c->ninputs, obf->op->rchunker,
                                     cp->n - has_consts, ell, q, obf->op->sigma);
    ref_list *deps = ref_list_new(c);
    threadpool *pool = threadpool_create(nthreads);
    g_max_npowers = 0;

    if (input_syms == NULL)
        goto finish;

    for (size_t ref = 0; ref < acirc_nrefs(c); ref++) {
        acirc_operation op = c->gates.gates[ref].op;
        if (!(op == OP_INPUT || op == OP_CONST))
            continue;
        work_args *args = calloc(1, sizeof args[0]);
        args->mmap   = obf->mmap;
        args->ref    = ref;
        args->c      = c;
        args->inputs = input_syms;
        args->obf    = obf;
        args->mine   = mine;
        args->ready  = ready;
        args->cache  = cache;
        args->deps   = deps;
        args->pool   = pool;
        args->rop    = outputs;
        args->kappas = kappas;
        threadpool_add_job(pool, eval_worker, args);
    }
    ret = OK;

finish:
    threadpool_destroy(pool);

    if (kappa) {
        unsigned int maxkappa = 0;
        for (size_t i = 0; i < noutputs; i++) {
            if (kappas[i] > maxkappa)
                maxkappa = kappas[i];
        }
        *kappa = maxkappa;
    }
    if (npowers)
        *npowers = g_max_npowers;

    for (size_t i = 0; i < acirc_nrefs(c); i++) {
        if (mine[i]) {
            encoding_free(obf->enc_vt, cache[i]);
        }
    }
    ref_list_free(deps, c);
    free(cache);
    free(mine);
    free(ready);
    free(kappas);
    free(input_syms);

    return ret;
}

obfuscator_vtable lz_obfuscator_vtable = {
    .free = _free,
    .obfuscate = _obfuscate,
    .evaluate = _evaluate,
    .fwrite = _fwrite,
    .fread = _fread,
};
