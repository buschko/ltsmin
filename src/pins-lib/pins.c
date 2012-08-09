#include <config.h>
#include <stdlib.h>

#include <dm/dm.h>
#include <hre/user.h>
#include <pins-lib/pins.h>
#include <util-lib/treedbs.h>

struct grey_box_model {
	model_t parent;
	lts_type_t ltstype;
	matrix_t *dm_info;
	matrix_t *dm_read_info;
	matrix_t *dm_write_info;
	matrix_t *sl_info;
    sl_group_t* sl_groups[GB_SL_GROUP_COUNT];
    guard_t** guards;
    matrix_t *gce_info; // guard co-enabled info
    matrix_t *gnes_info; // guard necessary enabling set
    matrix_t *gnds_info; // guard necessary disabling set
    int sl_idx_buchi_accept;
    int *por_visibility;
	int *s0;
	void*context;
	next_method_grey_t next_short;
	next_method_grey_t next_long;
	next_method_black_t next_all;
	get_label_method_t state_labels_short;
	get_label_method_t state_labels_long;
	get_label_group_method_t state_labels_group;
	get_label_all_method_t state_labels_all;
	transition_in_group_t transition_in_group;
	covered_by_grey_t covered_by;
    covered_by_grey_t covered_by_short;
	void* newmap_context;
	newmap_t newmap;
	int2chunk_t int2chunk;
	chunk2int_t chunk2int;
	get_count_t get_count;
	void** map;
};

struct nested_cb {
	model_t model;
	int group;
	int *src;
	TransitionCB cb;
	void* user_ctx;
};

void project_dest(void*context,transition_info_t*ti,int*dst){
#define info ((struct nested_cb*)context)
	int len = dm_ones_in_row(GBgetDMInfo(info->model), info->group);
	int short_dst[len];
	dm_project_vector(GBgetDMInfo(info->model), info->group, dst, short_dst);
	info->cb(info->user_ctx,ti,short_dst);
#undef info
}

int default_short(model_t self,int group,int*src,TransitionCB cb,void*context){
	struct nested_cb info;
	info.model = self;
	info.group = group;
	info.src=src;
	info.cb=cb;
	info.user_ctx=context;

	int long_src[dm_ncols(GBgetDMInfo(self))];
	dm_expand_vector(GBgetDMInfo(self), group, self->s0, src, long_src);
	return self->next_long(self,group,long_src,project_dest,&info);
}

void expand_dest(void*context,transition_info_t*ti,int*dst){
#define info ((struct nested_cb*)context)
	int long_dst[dm_ncols(GBgetDMInfo(info->model))];
	dm_expand_vector(GBgetDMInfo(info->model), info->group, info->src, dst, long_dst);
	info->cb(info->user_ctx,ti,long_dst);
#undef info
}

int default_long(model_t self,int group,int*src,TransitionCB cb,void*context){
	struct nested_cb info;
	info.model = self;
	info.group = group;
	info.src=src;
	info.cb=cb;
	info.user_ctx=context;

	int len = dm_ones_in_row(GBgetDMInfo(self), group);
	int src_short[len];
	dm_project_vector(GBgetDMInfo(self), group, src, src_short);

	return self->next_short(self,group,src_short,expand_dest,&info);
}

int default_all(model_t self,int*src,TransitionCB cb,void*context){
	int res=0;
	for(int i=0; i < dm_nrows(GBgetDMInfo(self)); i++) {
		res+=self->next_long(self,i,src,cb,context);
	}
	return res;
}

static int
state_labels_default_short(model_t model, int label, int *state)
{
    matrix_t *sl_info = GBgetStateLabelInfo(model);
    int long_state[dm_nrows(sl_info)];
    dm_expand_vector(sl_info, label, model->s0, state, long_state);
    return model->state_labels_long(model, label, long_state);
}

static int
state_labels_default_long(model_t model, int label, int *state)
{
    matrix_t *sl_info = GBgetStateLabelInfo(model);
    int len = dm_ones_in_row(sl_info, label);
    int short_state[len];
    dm_project_vector(sl_info, label, state, short_state);
    return model->state_labels_short(model, label, short_state);
}

static void
state_labels_default_group(model_t model, sl_group_enum_t group, int *state, int *labels)
{
    switch (group)
    {
        case GB_SL_ALL:
            GBgetStateLabelsAll(model, state, labels);
            return;
        case GB_SL_GUARDS:
            /**
             * This could potentially return a trivial guard for each transition group
             * by calling state next, return 1 if a next state is found and 0 if not.
             * This setup should be synchronized with the other guard functionality
             */
            Abort( "No default for guard group available in GBgetStateLabelsGroup" );
        default:
            Abort( "Unknown group in GBgetStateLabelsGroup" );
    }
}

static void
state_labels_default_all(model_t model, int *state, int *labels)
{
	for(int i=0;i<dm_nrows(GBgetStateLabelInfo(model));i++) {
		labels[i]=model->state_labels_long(model,i,state);
	}
}

static int
transition_in_group_default(model_t model, int* labels, int group)
{
    (void)model; (void)labels; (void)group;
    return 1;
}

int
wrapped_default_short (model_t self,int group,int*src,TransitionCB cb,void*context)
{
    return GBgetTransitionsShort (GBgetParent(self), group, src, cb, context);
}

int
wrapped_default_long (model_t self,int group,int*src,TransitionCB cb,void*context)
{
    return GBgetTransitionsLong (GBgetParent(self), group, src, cb, context);
}

int
wrapped_default_all (model_t self,int*src,TransitionCB cb,void*context)
{
    return GBgetTransitionsAll(GBgetParent(self), src, cb, context);
}

static int
wrapped_state_labels_default_short (model_t model, int label, int *state)
{
    return GBgetStateLabelShort(GBgetParent(model), label, state);
}

static int
wrapped_state_labels_default_long(model_t model, int label, int *state)
{
    return GBgetStateLabelLong(GBgetParent(model), label, state);
}

static void
wrapped_state_labels_default_group(model_t model, sl_group_enum_t group, int *state, int *labels)
{
    GBgetStateLabelsGroup(GBgetParent(model), group, state, labels);
}

static void
wrapped_state_labels_default_all(model_t model, int *state, int *labels)
{
    return GBgetStateLabelsAll(GBgetParent(model), state, labels);
}


model_t GBcreateBase(){
	model_t model=(model_t)RTmalloc(sizeof(struct grey_box_model));
    model->parent=NULL;
	model->ltstype=NULL;
	model->dm_info=NULL;
	model->dm_read_info=NULL;
	model->dm_write_info=NULL;
	model->sl_info=NULL;
    for(int i=0; i < GB_SL_GROUP_COUNT; i++)
        model->sl_groups[i]=NULL;
    model->guards=NULL;
    model->por_visibility=NULL;
    model->gce_info=NULL;
    model->gnes_info=NULL;
    model->gnds_info=NULL;
    model->sl_idx_buchi_accept = -1;
	model->s0=NULL;
	model->context=0;
	model->next_short=default_short;
	model->next_long=default_long;
	model->next_all=default_all;
	model->state_labels_short=state_labels_default_short;
	model->state_labels_long=state_labels_default_long;
	model->state_labels_group=state_labels_default_group;
	model->state_labels_all=state_labels_default_all;
	model->transition_in_group=transition_in_group_default;
	model->newmap_context=NULL;
	model->newmap=NULL;
	model->int2chunk=NULL;
	model->chunk2int=NULL;
	model->map=NULL;
	model->get_count=NULL;
	return model;
}

model_t
GBgetParent(model_t model)
{
    return model->parent;
}

void GBinitModelDefaults (model_t *p_model, model_t default_src)
{
    model_t model = *p_model;
    model->parent = default_src;
    if (model->ltstype == NULL) {
        GBcopyChunkMaps(model, default_src);
        GBsetLTStype(model, GBgetLTStype(default_src));
    }

    /* Copy dependency matrices. We cannot use the GBgetDMInfoRead and
     * GBgetDMInfoWrite calls here, as these default to the combined
     * matrix of the parent, which can be the wrong matrices in case
     * a certain pins2pins layer has restricted functionality and only
     * overwrites the combined matrix (like the regrouping layer before
     * becoming aware of the read and write matrices).  In this degenerated
     * case do the conservative thing and use the combined matrix that was
     * set.
     */
    if (model->dm_read_info == NULL) {
        if (model->dm_info == NULL)
            model->dm_read_info = default_src->dm_read_info;
        else
            model->dm_read_info = model->dm_info;
    }
    if (model->dm_write_info == NULL) {
        if (model->dm_info == NULL)
            model->dm_write_info = default_src->dm_write_info;
        else
            model->dm_write_info = model->dm_info;
    }
    if (model->dm_info == NULL)
        model->dm_info = default_src->dm_info;

    if (model->sl_info == NULL)
        GBsetStateLabelInfo(model, GBgetStateLabelInfo(default_src));

    for(int i=0; i < GB_SL_GROUP_COUNT; i++)
        GBsetStateLabelGroupInfo(model, i, GBgetStateLabelGroupInfo(default_src, i));

    if (model->guards == NULL)
        GBsetGuardsInfo(model, GBgetGuardsInfo(default_src));

    if (model->gce_info == NULL)
        GBsetGuardCoEnabledInfo(model, GBgetGuardCoEnabledInfo (default_src));

    if (model->por_visibility == NULL)
        GBsetPorVisibility (model, GBgetPorVisibility(default_src));

    if (model->gnes_info == NULL)
        GBsetGuardNESInfo(model, GBgetGuardNESInfo (default_src));

    if (model->gnds_info == NULL)
        GBsetGuardNDSInfo(model, GBgetGuardNDSInfo (default_src));

    if (model->sl_idx_buchi_accept < 0)
        GBsetAcceptingStateLabelIndex(model, GBgetAcceptingStateLabelIndex (default_src));

    if (model->s0 == NULL) {
        int N = lts_type_get_state_length (GBgetLTStype (default_src));
        int s0[N];
        GBgetInitialState(default_src, s0);
        GBsetInitialState(model, s0);
    }
    if (model->context == NULL)
        GBsetContext(model, GBgetContext(default_src));

    /* Since the model->next_{short,long,all} functions have mutually
     * recursive implementations, at least one needs to be overridden,
     * and the others need to call the overridden one (and not the
     * ones in the parent layer).
     *
     * If neither function is overridden, we pass through to the
     * parent layer.  However, we need to strip down the passed
     * model_t parameter, hence the wrapped_* functions.
     *
     * This scheme has subtle consequences: Assume a wrapper which
     * only implements a next_short function, but no next_long or
     * next_all.  If next_all is called, it will end up in the
     * next_short call (via the mutually recursive default
     * implementations), and eventually call through to the parent
     * layer's next_short.
     *
     * Hence, even if the parent layer also provides an optimized
     * next_all, it will never be called, unless the wrapper also
     * implements a next_all.
     */
    if (model->next_short == default_short &&
        model->next_long == default_long &&
        model->next_all == default_all) {
        GBsetNextStateShort (model, wrapped_default_short);
        GBsetNextStateLong (model, wrapped_default_long);
        GBsetNextStateAll (model, wrapped_default_all);
    }

    if (model->state_labels_short == state_labels_default_short &&
        model->state_labels_long == state_labels_default_long &&
        model->state_labels_all == state_labels_default_all) {
        GBsetStateLabelShort (model, wrapped_state_labels_default_short);
        GBsetStateLabelLong (model, wrapped_state_labels_default_long);
        GBsetStateLabelsGroup (model, wrapped_state_labels_default_group);
        GBsetStateLabelsAll (model, wrapped_state_labels_default_all);
    }
}

void* GBgetContext(model_t model){
	return model->context;
}
void GBsetContext(model_t model,void* context){
	model->context=context;
}

void GBsetLTStype(model_t model,lts_type_t info){
	if (model->ltstype != NULL)  Abort("ltstype already set");
    lts_type_validate(info);
	model->ltstype=info;
    if (model->map==NULL){
	    int N=lts_type_get_type_count(info);
	    model->map=RTmallocZero(N*sizeof(void*));
	    for(int i=0;i<N;i++){
		    model->map[i]=model->newmap(model->newmap_context);
	    }
    }
}

lts_type_t GBgetLTStype(model_t model){
	return model->ltstype;
}

void GBsetDMInfo(model_t model, matrix_t *dm_info) {
	if (model->dm_info != NULL) Abort("dependency matrix already set");
	model->dm_info=dm_info;
}

matrix_t *GBgetDMInfo(model_t model) {
	return model->dm_info;
}

void GBsetDMInfoRead(model_t model, matrix_t *dm_info) {
	if (model->dm_read_info != NULL) Abort("dependency matrix already set");
	model->dm_read_info=dm_info;
}

matrix_t *GBgetDMInfoRead(model_t model) {
	if (model->dm_read_info == NULL) {
        Warning(info, "read dependency matrix not set, returning combined matrix");
        return model->dm_info;
    }
	return model->dm_read_info;
}

void GBsetDMInfoWrite(model_t model, matrix_t *dm_info) {
	if (model->dm_write_info != NULL) Abort("dependency matrix already set");
	model->dm_write_info=dm_info;
}

matrix_t *GBgetDMInfoWrite(model_t model) {
	if (model->dm_write_info == NULL) {
        Warning(info, "write dependency matrix not set, returning combined matrix");
        return model->dm_info;
    }
	return model->dm_write_info;
}

void GBsetStateLabelInfo(model_t model, matrix_t *info){
	if (model->sl_info != NULL)  Abort("state info already set");
	model->sl_info=info;
}

matrix_t *GBgetStateLabelInfo(model_t model){
	return model->sl_info;
}

void GBsetInitialState(model_t model,int *state){
	if (model->ltstype==NULL)
            Abort("must set ltstype before setting initial state");
	RTfree (model->s0);
	int len=lts_type_get_state_length(model->ltstype);
	model->s0=(int*)RTmalloc(len * sizeof(int));
	for(int i=0;i<len;i++){
		model->s0[i]=state[i];
	}
}

void GBgetInitialState(model_t model,int *state){
	int len=lts_type_get_state_length(model->ltstype);
	for(int i=0;i<len;i++){
		state[i]=model->s0[i];
	}
}

void GBsetNextStateShort(model_t model,next_method_grey_t method){
	model->next_short=method;
}

int GBgetTransitionsShort(model_t model,int group,int*src,TransitionCB cb,void*context){
	return model->next_short(model,group,src,cb,context);
}

void GBsetNextStateLong(model_t model,next_method_grey_t method){
	model->next_long=method;
}

int GBgetTransitionsLong(model_t model,int group,int*src,TransitionCB cb,void*context){
	return model->next_long(model,group,src,cb,context);
}

void GBsetIsCoveredBy(model_t model,covered_by_grey_t covered_by){
    model->covered_by = covered_by;
}

void GBsetIsCoveredByShort(model_t model,covered_by_grey_t covered_by_short){
    model->covered_by_short = covered_by_short;
}

int GBisCoveredByShort(model_t model,int*a,int*b) {
    if (NULL == model->covered_by_short)
        Abort("No symbolic comparison function (isCoveredByShort) present for loaded model.");
    return model->covered_by_short(a,b);
}

int GBisCoveredBy(model_t model,int*a,int*b) {
    if (NULL == model->covered_by)
        Abort("No symbolic comparison function (isCoveredBy) present for loaded model.");
    return model->covered_by(a,b);
}

void GBsetNextStateAll(model_t model,next_method_black_t method){
	model->next_all=method;
}

int GBgetTransitionsAll(model_t model,int*src,TransitionCB cb,void*context){
	return model->next_all(model,src,cb,context);
}

void GBsetStateLabelsAll(model_t model,get_label_all_method_t method){
	model->state_labels_all=method;
}

void GBsetStateLabelsGroup(model_t model,get_label_group_method_t method){
	model->state_labels_group=method;
}

void GBsetStateLabelLong(model_t model,get_label_method_t method){
	model->state_labels_long=method;
}

void GBsetStateLabelShort(model_t model,get_label_method_t method){
	model->state_labels_short=method;
}

int GBgetStateLabelShort(model_t model,int label,int *state){
	return model->state_labels_short(model,label,state);
}

int GBgetStateLabelLong(model_t model,int label,int *state){
	return model->state_labels_long(model,label,state);
}

void GBgetStateLabelsGroup(model_t model,sl_group_enum_t group,int*state,int*labels){
	model->state_labels_group(model,group,state,labels);
}

void GBgetStateLabelsAll(model_t model,int*state,int*labels){
	model->state_labels_all(model,state,labels);
}

sl_group_t* GBgetStateLabelGroupInfo(model_t model, sl_group_enum_t group) {
    return model->sl_groups[group];
}

void GBsetStateLabelGroupInfo(model_t model, sl_group_enum_t group, sl_group_t* group_info)
{
    model->sl_groups[group] = group_info;
}

int GBhasGuardsInfo(model_t model) { return model->guards != NULL; }

void GBsetGuardsInfo(model_t model, guard_t** guards) {
    model->guards = guards;
}

guard_t** GBgetGuardsInfo(model_t model) {
    return model->guards;
}

void GBsetGuard(model_t model, int group, guard_t* guard) {
    model->guards[group] = guard;
}

guard_t* GBgetGuard(model_t model, int group) {
    return model->guards[group];
}

void GBsetGuardCoEnabledInfo(model_t model, matrix_t *info) {
    if (model->gce_info != NULL) Abort("guard may be co-enabled info already set");
    model->gce_info = info;
}

void GBsetPorVisibility(model_t model, int*visibility) {
    if (model->por_visibility != NULL) {
        //Warning(info, "POR visibility already set");
        RTfree(model->por_visibility);
        model->por_visibility = visibility;
    } else {
        model->por_visibility = visibility;
    }
}

int *GBgetPorVisibility(model_t model) {
    return model->por_visibility;
}

matrix_t *GBgetGuardCoEnabledInfo(model_t model) {
    return model->gce_info;
}

void GBsetGuardNESInfo(model_t model, matrix_t *info) {
    if (model->gnes_info != NULL) Abort("guard NES info already set");
    model->gnes_info = info;
}

matrix_t *GBgetGuardNESInfo(model_t model) {
    return model->gnes_info;
}

void GBsetGuardNDSInfo(model_t model, matrix_t *info) {
    if (model->gnds_info != NULL) Abort("guard NDS info already set");
    model->gnds_info = info;
}

matrix_t *GBgetGuardNDSInfo(model_t model) {
    return model->gnds_info;
}


void GBsetTransitionInGroup(model_t model,transition_in_group_t method){
	model->transition_in_group=method;
}

int GBtransitionInGroup(model_t model,int* labels,int group){
	return model->transition_in_group(model,labels,group);
}

void GBsetChunkMethods(model_t model,newmap_t newmap,void*newmap_context,
	int2chunk_t int2chunk,chunk2int_t chunk2int,get_count_t get_count){
	model->newmap_context=newmap_context;
	model->newmap=newmap;
	model->int2chunk=int2chunk;
	model->chunk2int=chunk2int;
	model->get_count=get_count;
}

void GBcopyChunkMaps(model_t dst, model_t src)
/* XXX This method should be removed when factoring out the chunk
 * business from the PINS interface.  If src->map is replaced after
 * copying, bad things are likely to happen when dst is used.
 */
{
    dst->newmap_context = src->newmap_context;
    dst->newmap = src->newmap;
    dst->int2chunk = src->int2chunk;
    dst->chunk2int = src->chunk2int;
    dst->get_count = src->get_count;

    int N    = lts_type_get_type_count(GBgetLTStype(src));
    dst->map = RTmallocZero(N*sizeof(void*));
    for(int i = 0; i < N; i++)
        dst->map[i] = src->map[i];
}

void GBgrowChunkMaps(model_t model, int old_n)
{
    void **old_map = model->map;
    int N=lts_type_get_type_count(GBgetLTStype(model));
    model->map=RTmallocZero(N*sizeof(void*));
    for(int i=0;i<N;i++){
        if (i < old_n) {
            model->map[i] = old_map[i];
        } else {
            model->map[i]=model->newmap(model->newmap_context);
        }
    }
    RTfree (old_map);
}

int GBchunkPut(model_t model,int type_no,const chunk c){
	return model->chunk2int(model->map[type_no],c.data,c.len);
}

chunk GBchunkGet(model_t model,int type_no,int chunk_no){
	chunk_len len;
	int tmp;
	char* data=(char*)model->int2chunk(model->map[type_no],chunk_no,&tmp);
	len=(chunk_len)tmp;
	return chunk_ld(len,data);
}

int GBchunkCount(model_t model,int type_no){
	return model->get_count(model->map[type_no]);
}


void GBprintDependencyMatrix(FILE* file, model_t model) {
	dm_print(file, GBgetDMInfo(model));
}

void GBprintDependencyMatrixRead(FILE* file, model_t model) {
	dm_print(file, GBgetDMInfoRead(model));
}

void GBprintDependencyMatrixWrite(FILE* file, model_t model) {
	dm_print(file, GBgetDMInfoWrite(model));
}

void GBprintDependencyMatrixCombined(FILE* file, model_t model) {
    matrix_t *dm   = GBgetDMInfo(model);
    matrix_t *dm_r = GBgetDMInfoRead(model);
    matrix_t *dm_w = GBgetDMInfoWrite(model);

    for (int i = 0; i < dm_nrows(dm); i++) {
        for (int j = 0; j < dm_ncols(dm); j++) {
            if (dm_is_set(dm_r, i, j) && dm_is_set(dm_w, i, j)) {
                fprintf(file, "+");
            } else if (dm_is_set(dm_r, i, j)) {
                fprintf(file, "r");
            } else if (dm_is_set(dm_w, i, j)) {
                fprintf(file, "w");
            } else {
                fprintf(file, "-");
            }
        }
        fprintf(file, "\n");
    }
}

/**********************************************************************
 * Grey box factory functionality
 */

#define MAX_TYPES 16
static char* model_type[MAX_TYPES];
static pins_loader_t model_loader[MAX_TYPES];
static int registered=0;
static char* model_type_pre[MAX_TYPES];
static pins_loader_t model_preloader[MAX_TYPES];
static int registered_pre=0;
static int matrix=0;
static int labels=0;
static int cache=0;
static int por=0;
static const char *regroup_options = NULL;

static char *ltl_file = NULL;
static const char *ltl_semantics = "spin";
static pins_ltl_type_t ltl_type = PINS_LTL_SPIN;

static si_map_entry db_ltl_semantics[]={
    {"spin",    PINS_LTL_SPIN},
    {"textbook",PINS_LTL_TEXTBOOK},
    {"ltsmin",  PINS_LTL_LTSMIN},
    {NULL, 0}
};

static void
ltl_popt (poptContext con, enum poptCallbackReason reason,
          const struct poptOption *opt, const char *arg, void *data)
{
    (void)con; (void)opt; (void)arg; (void)data;
    switch (reason) {
    case POPT_CALLBACK_REASON_PRE:
        break;
    case POPT_CALLBACK_REASON_POST:
        {
            int l = linear_search (db_ltl_semantics, ltl_semantics);
            if (l < 0) {
                Warning (error, "unknown ltl semantic %s", ltl_semantics);
                HREprintUsage();
                HREexit(EXIT_FAILURE);
            }
            ltl_type = l;
        }
        return;
    case POPT_CALLBACK_REASON_OPTION:
        break;
    }
    Abort("unexpected call to ltl_popt");
}

void chunk_table_print(log_t log, model_t model) {
    lts_type_t t = GBgetLTStype(model);
    log_printf(log,"The registered types values are:\n");
    int N=lts_type_get_type_count(t);
    int idx = 0;
    for(int i=0;i<N;i++){
        int V = GBchunkCount(model, i);
        for(int j=0;j<V;j++){
            char *type = lts_type_get_type(t, i);
            chunk c = GBchunkGet(model, i, j);
            char name[c.len*2+6];
            chunk2string(c, sizeof name, name);
            log_printf(log,"%4d: %s (%s)\n",idx, name, type);
            idx++;
        }
    }
}

void
GBloadFile (model_t model, const char *filename, model_t *wrapped)
{
    char               *extension = strrchr (filename, '.');
    if (extension) {
        extension++;
        for (int i = 0; i < registered; i++) {
            if (0==strcmp (model_type[i], extension)) {
                model_loader[i] (model, filename);
                if (wrapped) {
                    if (por)
                        model = GBaddPOR (model, ltl_file != NULL);
                    if (ltl_file)
                        model = GBaddLTL (model, ltl_file, ltl_type, por ? model : NULL);
                    if (regroup_options != NULL)
                        model = GBregroup (model, regroup_options);
                    if (cache)
                        model = GBaddCache (model);
                    *wrapped = model;
                }

                if (matrix) {
                    GBprintDependencyMatrixCombined(stdout, model);
                    exit (EXIT_SUCCESS);
                } else if (labels) {
                    lts_type_print(info, GBgetLTStype(model));
                    chunk_table_print(info, model);
                    exit (EXIT_SUCCESS);
                } else {
                    return;
                }
            }
        }
        Abort("No factory method has been registered for %s models",
               extension);
    } else {
        Abort("filename %s doesn't have an extension",
               filename);
    }
}

void
GBloadFileShared (model_t model, const char *filename)
{
    char               *extension = strrchr (filename, '.');
    if (extension) {
        extension++;
        for (int i = 0; i < registered_pre; i++) {
            if (0==strcmp (model_type_pre[i], extension)) {
                model_preloader[i] (model, filename);
                return;
            }
        }
    } else {
        Abort("filename %s doesn't have an extension", filename);
    }
}

void GBregisterLoader(const char*extension,pins_loader_t loader){
	if (registered<MAX_TYPES){
		model_type[registered]=strdup(extension);
		model_loader[registered]=loader;
		registered++;
	} else {
		Abort("model type registry overflow");
	}
}

void GBregisterPreLoader(const char*extension,pins_loader_t loader){
    if (registered_pre<MAX_TYPES){
        model_type_pre[registered_pre]=strdup(extension);
        model_preloader[registered_pre]=loader;
        registered_pre++;
    } else {
        Abort("model type registry overflow");
    }
}

int
GBgetAcceptingStateLabelIndex (model_t model)
{
    return model->sl_idx_buchi_accept;
}

int
GBsetAcceptingStateLabelIndex (model_t model, int idx)
{
    int oldidx = model->sl_idx_buchi_accept;
    model->sl_idx_buchi_accept = idx;
    return oldidx;
}

int
GBbuchiIsAccepting (model_t model, int *state)
{
    return GBgetAcceptingStateLabelIndex(model) >= 0 &&
        GBgetStateLabelLong(model, GBgetAcceptingStateLabelIndex(model), state);
}

struct poptOption ltl_options[] = {
    {NULL, 0, POPT_ARG_CALLBACK | POPT_CBFLAG_POST | POPT_CBFLAG_SKIPOPTION, (void *)ltl_popt, 0, NULL, NULL},
    {"ltl", 0, POPT_ARG_STRING, &ltl_file, 0, "LTL formula or file with LTL formula",
     "<ltl-file>.ltl|<ltl formula>"},
    {"ltl-semantics", 0, POPT_ARG_STRING | POPT_ARGFLAG_SHOW_DEFAULT, &ltl_semantics, 0,
     "LTL semantics", "<spin|textbook|ltsmin>"},
    POPT_TABLEEND
};

struct poptOption greybox_options[]={
    { "labels", 0, POPT_ARG_VAL, &labels, 1, "print state variable and type names, and state and action labels", NULL },
	{ "matrix" , 'm' , POPT_ARG_VAL , &matrix , 1 , "print the dependency matrix for the model and exit" , NULL},
	{ "por" , 'p' , POPT_ARG_VAL , &por , 1 , "enable partial order reduction" , NULL },
	{ "cache" , 'c' , POPT_ARG_VAL , &cache , 1 , "enable caching of grey box calls" , NULL },
	{ "regroup" , 'r' , POPT_ARG_STRING, &regroup_options , 0 ,
          "enable regrouping; available transformations T: "
          "gs, ga, gsa, gc, gr, cs, cn, cw, ca, csa, rs, rn, ru", "<(T,)+>" },
	{ NULL, 0 , POPT_ARG_INCLUDE_TABLE, ltl_options , 0 , "LTL options", NULL },
	POPT_TABLEEND	
};

void*
GBgetChunkMap(model_t model,int type_no)
{
	return model->map[type_no];
}
