#include <stddef.h>
#include <stdint.h>

#include <yajl/yajl_gen.h>
#include <yajl/yajl_tree.h>
#include <yajl/yajl_parse.h>
#include <curl/curl.h>


/*curl,yajl mockuped functions
********************************************/

enum { YAJL_GEN_ALLOC=0, YAJL_GEN_CONFIG, YAJL_GEN_MAP_OPEN, YAJL_GEN_MAP_CLOSE, 
       YAJL_GEN_ARRAY_OPEN, YAJL_GEN_ARRAY_CLOSE, YAJL_GEN_STRING, YAJL_GEN_NULL, 
       YAJL_GEN_INTEGER, YAJL_GEN_DOUBLE, YAJL_GEN_GET_BUF, CURL_EASY_SETOPT, 
       CURL_EASY_INIT, CURL_EASY_PERFORM, CURL_GLOBAL_INIT, YAJL_TREE_PARSE, 
       YAJL_TREE_GET, CURL_EASY_STRERROR, CURL_EASY_GETINFO, 
       MOCKS_COUNT
};

/*table of functions results*/
#define MOCK_VALUES_COUNT 10
#define NOMEMORY 1
const intptr_t s_mocks_logic_matrix[MOCKS_COUNT][MOCK_VALUES_COUNT] = {
    {1, 0, 1, 1, 1, 1, 1, 1, 1}, /*YAJL_GEN_ALLOC;  0:error, 1:ok*/
    {0, 0, 0, 0, 0, 0, 0, 0, 0}, /*YAJL_GEN_CONFIG*/
    {0, 0, 0, 0, 0, 0, 0, 0, 0}, /*YAJL_GEN_MAP_OPEN*/
    {0, 0, 0, 0, 0, 0, 0, 0, 0}, /*YAJL_GEN_MAP_CLOSE*/
    {0, 0, 0, 0, 0, 0, 0, 0, 0}, /*YAJL_GEN_ARRAY_OPEN*/
    {0, 0, 0, 0, 0, 0, 0, 0, 0}, /*YAJL_GEN_ARRAY_CLOSE*/
    {0, 0, 0, 0, 0, 0, 0, 0, 0}, /*YAJL_GEN_STRING*/
    {0, 0, 0, 0, 0, 0, 0, 0, 0}, /*YAJL_GEN_NULL*/
    {0, 0, 0, 0, 0, 0, 0, 0, 0}, /*YAJL_GEN_INTEGER*/
    {0, 0, 0, 0, 0, 0, 0, 0, 0}, /*YAJL_GEN_DOUBLE*/
    {0, 0, 0, 0, 0, 0, 0, 0, 0}, /*YAJL_GEN_GET_BUF*/
    {0, 0, 0, 0, 0,-1, 0, 0, 0}, /*CURL_EASY_SETOPT; 0:ok, -1:error*/
    {0, 1, 1, 1, 1, 1, 1, 1, 1}, /*CURL_EASY_INIT;   0:error, 1:ok*/
    {0, 0, 0, 0, 1, 0, 0, 0, 0}, /*CURL_EASY_PERFORM 0:ok, 1:error*/
    {0, 0,-1, 0, 0, 0, 0, 0, 0}, /*CURL_GLOBAL_INIT; 0:ok, -1:error*/

    {0, 0, 0, 0, 0, 0, 0, 0, 0}, /*YAJL_TREE_PARSE*/
    {0, 0, 0, 0, 0, 0, 0, 0, 0}, /*YAJL_TREE_GET*/
    {0, 0, 0, 0, 0, 0, 0, 0, 0}, /*CURL_EASY_STRERROR*/
    {0, 0, 0, 0, 0, 0, 0, 0, 0}  /*CURL_EASY_GETINFO*/
};
int s_test_index=0;

void init_mock_test(int index) {
	s_test_index=index;
}

yajl_gen yajl_gen_alloc (const yajl_alloc_funcs *allocFuncs){
	(void)allocFuncs;
	return (yajl_gen)s_mocks_logic_matrix[YAJL_GEN_ALLOC][s_test_index];
}
int yajl_gen_config (yajl_gen g, yajl_gen_option opt,...){
	(void)g;
	(void)opt;
	return (int)s_mocks_logic_matrix[YAJL_GEN_CONFIG][s_test_index];
}
yajl_gen_status yajl_gen_integer (yajl_gen hand, long long int number){
	(void)hand;
	(void)number;
	return (yajl_gen_status)s_mocks_logic_matrix[YAJL_GEN_INTEGER][s_test_index];
}
yajl_gen_status yajl_gen_double (yajl_gen hand, double number){
	(void)hand;
	(void)number;
	return (yajl_gen_status)s_mocks_logic_matrix[YAJL_GEN_DOUBLE][s_test_index];
}
yajl_gen_status yajl_gen_string (yajl_gen hand, const unsigned char *str, size_t len){
	return (yajl_gen_status)s_mocks_logic_matrix[YAJL_GEN_STRING][s_test_index];
}
 
yajl_gen_status yajl_gen_null (yajl_gen hand){
	return (yajl_gen_status)s_mocks_logic_matrix[YAJL_GEN_NULL][s_test_index];
	(void)hand;
}
 
yajl_gen_status yajl_gen_map_open (yajl_gen hand){
	return (yajl_gen_status)s_mocks_logic_matrix[YAJL_GEN_MAP_OPEN][s_test_index];
	(void)hand;
}
 
yajl_gen_status yajl_gen_map_close (yajl_gen hand){
	return (yajl_gen_status)s_mocks_logic_matrix[YAJL_GEN_MAP_CLOSE][s_test_index];
	(void)hand;
}
 
yajl_gen_status yajl_gen_array_open (yajl_gen hand){
	return (yajl_gen_status)s_mocks_logic_matrix[YAJL_GEN_ARRAY_OPEN][s_test_index];
	(void)hand;
}
 
yajl_gen_status yajl_gen_array_close (yajl_gen hand){
	return (yajl_gen_status)s_mocks_logic_matrix[YAJL_GEN_ARRAY_CLOSE][s_test_index];
	(void)hand;
}
yajl_gen_status yajl_gen_get_buf (yajl_gen hand, const unsigned char **buf, size_t *len){
	return (yajl_gen_status)s_mocks_logic_matrix[YAJL_GEN_GET_BUF][s_test_index];
	(void)hand;
	(void)buf;
	(void)len;
}
void yajl_gen_clear (yajl_gen hand){
	(void)hand;
}
void yajl_gen_free (yajl_gen handle){
	(void)handle;
}

yajl_val yajl_tree_parse(const char *input, char *error_buffer, size_t error_buffer_size){
	return (yajl_val)s_mocks_logic_matrix[YAJL_TREE_PARSE][s_test_index];
	(void)input;
	(void)error_buffer;
	(void)error_buffer_size;
}

yajl_val yajl_tree_get(yajl_val parent, const char **path, yajl_type type){
	return (yajl_val)s_mocks_logic_matrix[YAJL_TREE_GET][s_test_index];
	(void)parent;
	(void)path;
	(void)type;
}

void yajl_tree_free(yajl_val v){
    (void)v;
}


#undef curl_easy_setopt
CURLcode curl_easy_setopt(CURL *handle, CURLoption option, ...){
	(void)handle;
	(void)option;
	return (CURLcode)s_mocks_logic_matrix[CURL_EASY_SETOPT][s_test_index];
}
CURL *curl_easy_init( ){
	return (CURL *)s_mocks_logic_matrix[CURL_EASY_INIT][s_test_index];
}
void curl_easy_cleanup(CURL * handle ){
	(void)handle;
}
struct curl_slist *curl_slist_append(struct curl_slist * list, const char * string ){
	(void)list;
	(void)string;
	return NULL;
}
CURLcode curl_easy_perform(CURL * easy_handle ){
	(void)easy_handle;
	return (CURLcode)s_mocks_logic_matrix[CURL_EASY_PERFORM][s_test_index];
}
CURLcode curl_global_init(long flags ){
	(void)flags;
	return s_mocks_logic_matrix[CURL_GLOBAL_INIT][s_test_index];
}
void curl_global_cleanup(void){
}

const char *curl_easy_strerror(CURLcode errornum){
	return NULL;
}

void curl_slist_free_all(struct curl_slist * list){
	(void)list;
}

#undef curl_easy_getinfo
CURLcode curl_easy_getinfo(CURL *curl, CURLINFO info, ...){
	return s_mocks_logic_matrix[CURL_EASY_GETINFO][s_test_index];
	(void)curl;
	(void)info;
}

/********************************************
curl,yajl mockuped functions*/
