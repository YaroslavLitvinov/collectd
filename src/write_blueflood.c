/**
 * collectd - src/write_blueflood.c
 * Copyright (C) 2009       Paul Sadauskas
 * Copyright (C) 2009       Doug MacEachern
 * Copyright (C) 2007-2014  Florian octo Forster
 * Copyright (C) 2014       Rackspace
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; only version 2 of the License is applicable.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St, Fifth Floor, Boston, MA  02110-1301 USA
 *
 * Authors:
 *   Florian octo Forster <octo at collectd.org>
 *   Doug MacEachern <dougm@hyperic.com>
 *   Paul Sadauskas <psadauskas@gmail.com>
 *   Yaroslav Litvinov <yaroslav.litvinov@rackspace.com>
 **/

#include <assert.h>

#if HAVE_PTHREAD_H
# include <pthread.h>
#endif

#include <yajl/yajl_gen.h>
#include <yajl/yajl_tree.h>
#include <yajl/yajl_parse.h>
#include <curl/curl.h>
  
#include "collectd.h"
#include "plugin.h"
#include "common.h"

#ifndef WRITE_HTTP_DEFAULT_BUFFER_SIZE
# define WRITE_HTTP_DEFAULT_BUFFER_SIZE 4096
#endif

#define PLUGIN_NAME "write_blueflood"
#define MAX_METRIC_NAME_SIZE (6*DATA_MAX_NAME_LEN)
#define MAX_URL_SIZE 128
/* second count in day */
#define DEFAULT_TTL 24 * 60 * 60


/* used by transport */
#define CURL_SETOPT_RETURN_ERR(option, parameter){			\
		CURLcode err;						\
		if ( CURLE_OK != (err=curl_easy_setopt(curl, option, parameter)) ){ \
			return err;					\
		}							\
	}

/* used by json generator */
#define YAJL_CHECK_RETURN_ON_ERROR(func){	\
		yajl_gen_status s = func;	\
		if ( s!=yajl_gen_status_ok ){	\
			return s;		\
		}				\
	}
  
#define YAJL_ERROR_BUF_MAX_SIZE 1024

/* literals for json output */
#define STR_NAME "metricName"
#define STR_VALUE "metricValue"
#define STR_TIMESTAMP "collectionTime"
#define STR_TTL CONF_TTL

/* config literals */
#define CONF_TTL "ttlInSeconds"
#define CONF_AUTH_USER "User"
#define CONF_AUTH_PASSORD "Password"
#define CONF_URL "URL"  
#define CONF_AUTH_URL "Auth_URL"  
#define CONF_TENANTID "TenantID"
  
#define BLUEFLOOD_API_VERSION "v2.0"

struct wb_transport_s;
  
typedef struct wb_callback_s
{
	struct wb_transport_s* 	userp;
 
	yajl_gen yajl_gen;
	pthread_mutex_t send_lock;
} wb_callback_t;

/* rax auth */
const char* s_blueflood_ingest_url_template = "%s/"BLUEFLOOD_API_VERSION"/%s/ingest";

static char* blueflood_get_ingest_url(char* buffer, const char* url, const char* tenant)
{
	snprintf(buffer, MAX_URL_SIZE, s_blueflood_ingest_url_template, url, tenant);
	return buffer;
}
//!!comments, проверить можем ли мы использовать стриминг парсинг, вместо хранения всех кусков
struct MemoryStruct {
	char *memory;
	size_t size;
};



static int metric_format_name(char *ret, int ret_len, const char *hostname,
			      const char *plugin, const char *plugin_instance, const char *type,
			      const char *type_instance, const char *name)
{
	//!!вынести дефайны из функции, определить как константы внутри функции
	//!!аргументы в скобочки 
	//!!переделать полностью, меньше сложностей
#define MAX_PARAMS 6
#define SEPARATOR "."
#define INSTANCE_SEPARATOR "-"
#define STRNCATNULL(buff, str) strncat(buff, STRNULL(str), STRLENNULL(str))
#define STRLENNULL(str) (str == NULL?0:strlen(str))
#define STRNULL(str) (str == NULL?"":str)
	char *s = ret;
	if (!s)
		{
			//!!заменить принтф на сислоги
			//!!убрать излишние проверки на которые нет покрытия
			ERROR("Error. No buffer space available\n");
			return ENOBUFS;
		}
	size_t all_str_len = STRLENNULL(
					hostname) + STRLENNULL(plugin) + STRLENNULL(plugin_instance) +
		STRLENNULL(type) + STRLENNULL(type_instance) + STRLENNULL(name);
	if (all_str_len + MAX_PARAMS >= ret_len)
		{
			ERROR("Error. No buffer space available\n");
			return ENOBUFS;
		}
	s[0] = '\0';
	STRNCATNULL(s, hostname);
	//!!скобочки на каждый if 
	if (hostname)
		STRNCATNULL(s, SEPARATOR);
	STRNCATNULL(s, plugin);
	if ((plugin_instance != NULL) && (plugin_instance[0] != 0))
		{
			if (plugin)
				STRNCATNULL(s, INSTANCE_SEPARATOR);
			STRNCATNULL(s, plugin_instance);
		}
	if (plugin_instance || plugin)
		STRNCATNULL(s, SEPARATOR);
	STRNCATNULL(s, type);
	if ((type_instance != NULL) && (type_instance[0] != 0))
		{
			if (type)
				STRNCATNULL(s, INSTANCE_SEPARATOR);
			STRNCATNULL(s, type_instance);
		}
	if (type_instance || type)
		STRNCATNULL(s, SEPARATOR);
	STRNCATNULL(s, name);
	return 0;
}


/*************yajl json parsing implementation************/
/*parse and retrieve key
  @return parsed value is allocated in the heap*/
static char *json_get_key_alloc(const char **path, const char *buff)
{
	yajl_val node;
	char errbuf[YAJL_ERROR_BUF_MAX_SIZE];
	char *str_val = NULL;
	char *str_val_yajl = NULL;

	if (NULL==(node=yajl_tree_parse(buff, errbuf, sizeof(errbuf))))
		{
			if (strlen(errbuf))
				{
					ERROR("%s plugin: %s", PLUGIN_NAME, errbuf);
				}
			else
				{
					ERROR("%s plugin: unknown json parsing error", PLUGIN_NAME);	
				}
			return NULL;
		}
	str_val_yajl = YAJL_GET_STRING(yajl_tree_get(node, path, yajl_t_string));
	if (str_val_yajl) str_val = strdup(str_val_yajl);
	yajl_tree_free(node);
	return str_val;
}

/* Code stolen from curl documentation/samples */
/* See http://curl.haxx.se/libcurl/c/getinmemory.html */
/* TODO Replace with libyajl streaming read */
static size_t
curl_callback(void *contents, size_t size, size_t nmemb, void *userp)
{
	size_t realsize = size * nmemb;
	struct MemoryStruct *mem = (struct MemoryStruct *)userp;

	// hope that realsize > 0 and (mem->size + realsize + 1) < MAX_INT
	// are checked in libcurl...
	mem->memory = realloc(mem->memory, mem->size + realsize + 1);
	if (mem->memory == NULL) {
		ERROR("%s plugin: not enough memory", PLUGIN_NAME);
		return 0;
	}
	memcpy(&mem->memory[mem->size], contents, realsize);
	mem->size += realsize;
	mem->memory[mem->size] = 0;
	return realsize;
}

/*@return save auth header in static variable and return*/
const char *format_auth_header(const char *user, const char *pass)
{
	static char inbuffer[WRITE_HTTP_DEFAULT_BUFFER_SIZE];
	const char* rax_auth_template = 
		"{\"auth\":"
		"{\"RAX-KSKEY:apiKeyCredentials\":"
		"{\"username\":\"%s\","
		"\"apiKey\":\"%s\"}"
		"}"
		"}";

	snprintf(inbuffer, sizeof(inbuffer), rax_auth_template, user, pass);
	return inbuffer;
}

//!!не тащить аутентификационные параметры через код
static int auth(struct blueflood_curl_transport_t *transport,
		const char* url, const char* user, const char* pass, char** token, char** tenant) {
	int err=0;
	CURLcode res;
	struct MemoryStruct chunk;
	struct curl_slist *headers = NULL;
	const char* token_xpath[] = {"access", "token", "id", (const char* )0};
	const char* tenant_xpath[] = {"access", "token", "tenant", "id", (const char* )0};

	curl = curl_easy_init();

	/*prepare data for callback*/
	chunk.memory = malloc(WRITE_HTTP_DEFAULT_BUFFER_SIZE);
	chunk.size = 0;

	auth_request_setup(transport->curl, &headers, transport->curl_errbuf,
			   url, user, pass, &chunk);
	res = transport->public.send(&transport->public);

	/* Check for errors */
	if (res != CURLE_OK) {
		ERROR ("%s plugin: Auth request send error: %s", PLUGIN_NAME, 
		       transport->public.last_error_text(&transport->public));
		return 1;
	}
	// TODO delicately process errors
	sfree(*token);
	*token = json_get_key_alloc(token_xpath, chunk.memory);
	sfree(*tenant);
	*tenant = json_get_key_alloc(tenant_xpath, chunk.memory);

	if (!*token) {
		ERROR("%s plugin: Bad token returned", PLUGIN_NAME);
		err=-1;
	}
	if (!*tenant) {
		ERROR("%s plugin: Bad tenantdId returned", PLUGIN_NAME);
		err=-1;
	}

	sfree(chunk.memory);
	curl_slist_free_all(headers);
	return err;
}


/****************curl transport declaration*****************/
struct blueflood_transport_interface {
	int  (*ctor)(struct blueflood_transport_interface *this);
	void (*dtor)(struct blueflood_transport_interface *this);
	int  (*send)(struct blueflood_transport_interface *this, const char *buffer, size_t len);
	const char *(*last_error_text)(struct blueflood_transport_interface *this);
};

typedef struct data_s
{
	const char *url;
	const char *tenantid;  
} data_t;

typedef struct auth_data_s
{
	const char *auth_url;
	const char *user;
	const char *pass;  
  	char *token;
} auth_data_t;

struct blueflood_transport_t {
	struct blueflood_transport_interface public; /* it must be first item in the structure */
	/* data */
	CURL *curl;
	char curl_errbuf[CURL_ERROR_SIZE];
	data_t data;
	auth_data_t auth_data;
};

/* global variables */
struct blueflood_transport_interface *s_blueflood_transport;

/*************blueflood transport implementation************/

static int transport_ctor(struct blueflood_transport_interface *this ){
	struct blueflood_curl_transport_t *self = (struct blueflood_curl_transport_t *)this;
	assert( self->curl == NULL );
	self->curl = curl_easy_init();
	if (self->curl == NULL){
		strncpy(self->curl_errbuf, "libcurl: curl_easy_init failed.", CURL_ERROR_SIZE );
		return -1;
	}
	return 0;
}

static void transport_dtor(struct blueflood_transport_interface *this){
	struct blueflood_curl_transport_t *self = (struct blueflood_curl_transport_t *)this;
	if ( self->curl != NULL ){
		curl_easy_cleanup (self->curl);
		self->curl = NULL;
	}
	sfree(self->tenantid);
	sfree(self->token);
	sfree(self);
}

static int fill_headers(struct curl_slist** headers, const char* token)
{
	curl_slist_free_all(*headers);

	*headers = curl_slist_append(*headers, "Accept:  */*");
	*headers = curl_slist_append(*headers, "Content-Type: application/json");
	*headers = curl_slist_append(*headers, "Expect:");

	if (token)
		{
			static char url_buffer[MAX_URL_SIZE];
			snprintf(url_buffer, sizeof(url_buffer), "X-Auth-Token: %s", token);
			*headers = curl_slist_append(*headers, url_buffer);
		}

	return 0;
}

int blueflood_request_setup(CURL *curl, struct curl_slist **headers, char *curl_errbuf,
			    const char *url, const char *token, const char *buffer, size_t len)
{
	CURL_SETOPT_RETURN_ERR(CURLOPT_NOSIGNAL, 1L);
	CURL_SETOPT_RETURN_ERR(CURLOPT_USERAGENT, COLLECTD_USERAGENT"C");

	*headers = curl_slist_append(*headers, "Accept:  */*");
	*headers = curl_slist_append(*headers, "Content-Type: application/json");
	*headers = curl_slist_append(*headers, "Expect:");

	if (token!=NULL){
		static char token_header[MAX_URL_SIZE];
		snprintf(token_header, sizeof(token_header), "X-Auth-Token: %s", self->token);
		*headers = curl_slist_append(*headers, token_header);
	}

	CURL_SETOPT_RETURN_ERR(CURLOPT_HTTPHEADER, *headers);
	CURL_SETOPT_RETURN_ERR(CURLOPT_ERRORBUFFER, curl_errbuf);
	CURL_SETOPT_RETURN_ERR(CURLOPT_URL, url);
	CURL_SETOPT_RETURN_ERR(CURLOPT_POSTFIELDSIZE, len);
	CURL_SETOPT_RETURN_ERR(CURLOPT_POSTFIELDS, buffer);
}

int blueflood_with_auth_request_setup(CURL *curl, struct curl_slist **headers, char *curl_errbuf,
		       const char *url, const char *token, const char *tenantid, 
		       const char *buffer, size_t len)
{
	//TODO: move url constructing to xxx_request_setup paramaters
	char url_buffer[MAX_URL_SIZE];
	CURL_SETOPT_RETURN_ERR(CURLOPT_URL, blueflood_get_ingest_url(url_buffer, url, tenantid));	
}

int auth_request_setup(CURL *curl, struct curl_slist **headers, char *curl_errbuf,
		       const char *url, const char *user, const char *pass, 
		       struct MemoryStruct *chunk)
{
	//TODO: should expect headers is required or not
	CURL_SETOPT_RETURN_ERR(CURLOPT_NOSIGNAL, 1L);
	//TODO: user agent name for auth server and blueflood may be different?
	CURL_SETOPT_RETURN_ERR(CURLOPT_USERAGENT, COLLECTD_USERAGENT"C");
	CURL_SETOPT_RETURN_ERR(CURLOPT_ERRORBUFFER, curl_errbuf);
	CURL_SETOPT_RETURN_ERR(CURLOPT_URL, url);
	CURL_SETOPT_RETURN_ERR(CURLOPT_POSTFIELDSIZE, len);
	CURL_SETOPT_RETURN_ERR(CURLOPT_POSTFIELDS, buffer);

	CURL_SETOPT_RETURN_ERR(CURLOPT_POSTFIELDS, format_auth_header(user, pass));
	CURL_SETOPT_RETURN_ERR(CURLOPT_WRITEFUNCTION, curl_callback);
	CURL_SETOPT_RETURN_ERR(CURLOPT_WRITEDATA, chunk);

	*headers = curl_slist_append (*headers, "Accept:  */*");
	*headers = curl_slist_append (*headers, "Content-Type: application/json");
	*headers = curl_slist_append (*headers, "Expect:");
	CURL_SETOPT_RETURN_ERR(CURLOPT_HTTPHEADER, *headers);
	return 0;
}

            
            
static int transport_send(struct blueflood_transport_interface *this){
	char url_buffer[MAX_URL_SIZE];
	struct blueflood_curl_auth_trasnport_t *self = (struct blueflood_curl_transport_t *)this;
	CURLcode status = 0;
	status = curl_easy_perform (self->curl);
	// TODO: capture output as well or it will be printed to STDOUT (libcurl default)
	if (status != CURLE_OK){
		strncpy(self->curl_errbuf, "libcurl: curl_easy_perform failed.", CURL_ERROR_SIZE );
	}
	return status;
}            
            
static int auth_transport_send(struct blueflood_transport_interface *this, const char *buffer, size_t len){

	/***********************************************************************************************************
	  start session
	***********************************************************************************************************/
	char url_buffer[MAX_URL_SIZE];
	struct curl_slist *headers = NULL;
	struct blueflood_curl_auth_trasnport_t *self = (struct blueflood_curl_transport_t *)this;
	CURLcode status = 0;

	/* if auth_url is configured and token not yet exist */
	if (self->auth_url != NULL && !self->token) {
		auth(self->auth_url, self->user, self->pass, &self->token, &self->tenantid);
	}

	/*do not check here for CURL object, as it checked once in constructor*/
	CURL_SETOPT_RETURN_ERR(CURLOPT_NOSIGNAL, 1L);
	CURL_SETOPT_RETURN_ERR(CURLOPT_USERAGENT, COLLECTD_USERAGENT"C");

	fill_headers(&headers, self->token);
	CURL_SETOPT_RETURN_ERR(CURLOPT_HTTPHEADER, headers);
	CURL_SETOPT_RETURN_ERR(CURLOPT_ERRORBUFFER, self->curl_errbuf);
	CURL_SETOPT_RETURN_ERR(CURLOPT_URL, blueflood_get_ingest_url(url_buffer, self->url, self->tenantid));

	/***********************************************************************************************************
	  send
	***********************************************************************************************************/

	CURL_SETOPT_RETURN_ERR(CURLOPT_POSTFIELDSIZE, len);
	CURL_SETOPT_RETURN_ERR(CURLOPT_POSTFIELDS, buffer);
	status = curl_easy_perform (self->curl);
	if (status != CURLE_OK){
		strncpy(self->curl_errbuf, "libcurl: curl_easy_perform failed.", CURL_ERROR_SIZE );
	}
	curl_slist_free_all(headers);
	headers = NULL;

	/*if auth_url is configured then check and handle if needed auth errors*/
	if (self->auth_url !=NULL){
		/*check if we need to reauth (error code == 401)*/
		int code = 500;
		curl_easy_getinfo(self->curl, CURLINFO_RESPONSE_CODE, &code);
		if (code == 401 || code == 403) {
			// TODO check for errors
			// TODO do not close/delete yajl generator on network fail, try not to lose data
			auth(self->auth_url, self->user, self->pass, &self->token, &self->tenantid);

			/* Token could change after re auth procedure, so update X-Auth-Token header */
			fill_headers(&headers, self->token);
			CURL_SETOPT_RETURN_ERR(CURLOPT_HTTPHEADER, headers);
			CURL_SETOPT_RETURN_ERR(CURLOPT_URL, blueflood_get_ingest_url(url_buffer, self->url, self->tenantid));

			status = curl_easy_perform (self->curl);
			if (status != CURLE_OK){
				strncpy(self->curl_errbuf, "libcurl: curl_easy_perform failed.", CURL_ERROR_SIZE );
			}
			curl_slist_free_all(headers);
			headers = NULL;
		}
	}
	return status;
}


static const char *transport_last_error_text(struct blueflood_transport_interface *this){
	struct blueflood_curl_transport_t *self = (struct blueflood_curl_transport_t *)this;
	return self->curl_errbuf;
}

static struct blueflood_transport_interface s_blueflood_transport_interface = {
	transport_ctor,
	transport_dtor,
	auth_transport_send,
	transport_last_error_text
};


            
///////////////////////
struct blueflood_transport_interface* blueflood_curl_transport_alloc(const data_t* data, const auth_data_t* auth_data) {
	struct blueflood_curl_transport_t *self = calloc(1, sizeof(struct blueflood_transport_t));
	self->public = s_blueflood_transport_interface;
	self->public.ctor = transport_ctor;
	self->public.dtor = transport_dtor;
	self->public.send = transport_send;
	self->public.last_error_text = transport_last_error_text;
	self->data = data;
	self->auth_data = auth_data;
	if ( self->public.ctor(&self->public) == 0 )
		return &self->public;
	else {
		free(self);
		return NULL;
	}
}


void blueflood_curl_transport_free( struct blueflood_transport_interface **transport){
	if ( *transport!=NULL ){
		(*transport)->end_session(*transport);
		(*transport)->destroy(*transport);
		*transport = NULL;
	}
}


static int blueflood_curl_transport_global_initialize(long flags){
	/*As curl_global_init is not thread-safe it must be called a once
	  before start of using it*/
	return curl_global_init(flags);
}

static void blueflood_curl_transport_global_finalize(){
	curl_global_cleanup();
}

/*************yajl json generator implementation************/

static int jsongen_init(yajl_gen *gen){
	/*initialize yajl*/
	*gen = yajl_gen_alloc(NULL);
	if ( *gen != NULL ){
		yajl_gen_config(*gen, yajl_gen_beautify, 1);
		yajl_gen_config(*gen, yajl_gen_validate_utf8, 1);
		return 0;
	}
	else{
		ERROR ("%s plugin: yajl_gen_alloc error", PLUGIN_NAME );
		return -1;
	}
}

static int jsongen_map_key_value(yajl_gen gen, data_source_t *ds,
				 const value_list_t *vl, const value_t *value)
{
	char name_buffer[MAX_METRIC_NAME_SIZE];

	/*name's key*/
	YAJL_CHECK_RETURN_ON_ERROR(yajl_gen_string(gen, 
						   (const unsigned char *)STR_NAME,
						   strlen(STR_NAME)));
	metric_format_name(name_buffer, sizeof (name_buffer),
			   vl->host, vl->plugin, vl->plugin_instance,
			   vl->type, vl->type_instance, ds->name);

	/*name's value*/
	YAJL_CHECK_RETURN_ON_ERROR(yajl_gen_string(gen, 
						   (const unsigned char *)name_buffer,
						   strlen(name_buffer)));
	/*value' key*/
	YAJL_CHECK_RETURN_ON_ERROR(yajl_gen_string(gen, 
						   (const unsigned char *)STR_VALUE,
						   strlen(STR_VALUE)));
	/*value's value*/
	if ( ds->type == DS_TYPE_GAUGE ){
		if(isfinite (value->gauge)){
			YAJL_CHECK_RETURN_ON_ERROR(yajl_gen_double(gen, value->gauge));
		}
		else{
			YAJL_CHECK_RETURN_ON_ERROR(yajl_gen_null(gen));
		}
	}
	else if ( ds->type == DS_TYPE_COUNTER ){
		YAJL_CHECK_RETURN_ON_ERROR(yajl_gen_integer(gen, value->counter));
	}
	else if ( ds->type == DS_TYPE_ABSOLUTE ){
		YAJL_CHECK_RETURN_ON_ERROR(yajl_gen_integer(gen, value->absolute));
	}
	else if ( ds->type == DS_TYPE_DERIVE ){
		YAJL_CHECK_RETURN_ON_ERROR(yajl_gen_double(gen, value->derive));
	}
	else{
		WARNING ("%s plugin: can't handle unknown ds_type=%d", PLUGIN_NAME, ds->type);
	}
	return 0;
}

static int send_json_freemem(yajl_gen *gen){
	const unsigned char *buf;
	size_t len;
	/*cache flush & free memory */
	YAJL_CHECK_RETURN_ON_ERROR(yajl_gen_get_buf(*gen, &buf, &len));

	/* don't add anything to buffer if we don't have any data.*/
	if (len>0){
		/*end of json*/
		YAJL_CHECK_RETURN_ON_ERROR(yajl_gen_array_close(*gen));
	}

	/*cache flush & free memory */
	YAJL_CHECK_RETURN_ON_ERROR(yajl_gen_get_buf(*gen, &buf, &len));

	/*if have any data for sending*/
	if (len>0){
		struct curl_slist *headers=NULL;
		char url_buffer[MAX_URL_SIZE];
		/*with auth*/
		if (transport->auth_data.auth_url!=NULL)
			{
				struct MemoryStruct chunk;
				CURL_SETOPT_RETURN_ERR(CURLOPT_URL, blueflood_get_ingest_url(url_buffer, 
											     transport->data.url, 
											     transport->data.tenantid));	
				//TODO: check return error
				auth(s_blueflood_transport,
				     url_buffer, const char* user, const char* pass, char** token, char** tenant)

				int auth_request_setup(transport->curl, &headers, transport->curl_errbuf,
						       url_buffer, transport->auth_data.user, transport->auth_data.pass, 
						       &chunk);
				curl_slist_free_all(headers);
				headers = NULL;

			}
		else
			{
				strncpy(url_buffer, transport->data.url, sizeof(url_buffer));
			}

		/*without auth*/
		struct blueflood_transport_t *transport = (struct blueflood_transport_t *)s_blueflood_transport;

		//TODO: check return error
		int blueflood_request_setup(transport->curl, &headers, transport->curl_errbuf,
					    transport->data.url, transport->auth_data.token, 
					    buf, len);
		if ( s_blueflood_transport->send(s_blueflood_transport) != 0 ){
			ERROR ("%s plugin: Metrics (len=%zu) send error: %s", PLUGIN_NAME, len,
			       s_blueflood_transport->last_error_text(s_blueflood_transport));
		}
	}
	yajl_gen_free(*gen), *gen = NULL;

	if (jsongen_init(gen) != 0) {
		return -1;
	}
	return 0;
}

static int jsongen_output(wb_callback_t *cb, 
			  const data_set_t *ds,
			  const value_list_t *vl )
{
	int i;

	const unsigned char *buf;
	size_t len;
	YAJL_CHECK_RETURN_ON_ERROR(yajl_gen_get_buf(cb->yajl_gen, &buf, &len));
	if ( !len ){
		/*json beginning*/
		YAJL_CHECK_RETURN_ON_ERROR(yajl_gen_array_open(cb->yajl_gen));
	}

	for (i = 0; i < ds->ds_num; i++){
		YAJL_CHECK_RETURN_ON_ERROR(yajl_gen_map_open(cb->yajl_gen));

		jsongen_map_key_value(cb->yajl_gen, &ds->ds[i], vl, &vl->values[i]);

		/*key, value pair*/
		YAJL_CHECK_RETURN_ON_ERROR(yajl_gen_string(cb->yajl_gen,
							   (const unsigned char *)STR_TIMESTAMP,
							   strlen(STR_TIMESTAMP)));
		YAJL_CHECK_RETURN_ON_ERROR(yajl_gen_integer(cb->yajl_gen,
							    CDTIME_T_TO_MS (vl->time)));
		/*key, value pair*/
		YAJL_CHECK_RETURN_ON_ERROR(yajl_gen_string(cb->yajl_gen,
							   (const unsigned char *)STR_TTL,
							   strlen(STR_TTL)));
		YAJL_CHECK_RETURN_ON_ERROR(yajl_gen_integer(cb->yajl_gen,
							    cb->ttl));

		YAJL_CHECK_RETURN_ON_ERROR(yajl_gen_map_close(cb->yajl_gen));
	}

	return 0;
}


/*************blueflood plugin implementation************/

static void free_user_data(wb_callback_t *cb){
	if ( !cb ) return;

	if ( cb->yajl_gen != NULL ){
		send_json_freemem(&cb->yajl_gen);
		yajl_gen_free(cb->yajl_gen);
	}

	/*curl transport end session & destroy*/
	blueflood_curl_transport_free(&s_blueflood_transport);

	sfree (cb->url);
	sfree (cb->auth_url);
	sfree (cb->tenantid);
	sfree (cb->user);
	sfree (cb->pass);

	sfree (cb);
}

static void wb_callback_free (void *data){
	INFO ("%s plugin: free", PLUGIN_NAME);
	free_user_data((wb_callback_t *)data);
}

static int wb_write (const data_set_t *ds, const value_list_t *vl,
		     user_data_t *user_data){
	wb_callback_t *cb;
	int status;

	cb = user_data->data; 
	pthread_mutex_lock (&cb->send_lock);
	status = jsongen_output(cb, ds, vl);
	if ( status != 0 ){
		ERROR ("%s plugin: json generating failed err=%d.", PLUGIN_NAME, status);
		status = -1;
	}
	pthread_mutex_unlock (&cb->send_lock);
	return (status);
}

static int send_data(user_data_t *user_data) {
	wb_callback_t *cb;

	cb = user_data->data;
	pthread_mutex_lock (&cb->send_lock);
	send_json_freemem(&cb->yajl_gen);
	pthread_mutex_unlock (&cb->send_lock);
	return 0;
}

static int wb_flush (cdtime_t timeout __attribute__((unused)),
		     const char *identifier __attribute__((unused)),
		     user_data_t *user_data){
	return send_data(user_data);
}

static int wb_read(user_data_t *user_data)
{
	return send_data(user_data);
}

static void config_get_auth_params (oconfig_item_t *child, wb_callback_t *cb )
{
	int i = 0;
	cf_util_get_string(child, &cb->auth_url);
	for (i = 0; i < child->children_num; i++)
		{
			oconfig_item_t *childAuth = child->children + i;
				if (strcasecmp(CONF_AUTH_USER, childAuth->key) == 0)
					cf_util_get_string(childAuth, &cb->user);
				else if (strcasecmp(CONF_AUTH_PASSORD, childAuth->key) == 0)
					cf_util_get_string(childAuth, &cb->pass);
				else
					ERROR("%s plugin: Invalid configuration "
						  "option: %s.", PLUGIN_NAME, childAuth->key);
		}
}

static void config_get_url_params (oconfig_item_t *ci, wb_callback_t *cb)
{
	if (strcasecmp("URL", ci->key) == 0)
		{
			cb->ttl = DEFAULT_TTL;
			cf_util_get_string(ci, &cb->url);
			int i = 0;
			for (i = 0; i < ci->children_num; i++)
				{
					oconfig_item_t *child = ci->children + i;
						if (strcasecmp(CONF_TENANTID, child->key) == 0)
							cf_util_get_string(child, &cb->tenantid);
						else if (strcasecmp(CONF_TTL, child->key) == 0)
							cf_util_get_int(child, &cb->ttl);
						else if (strcasecmp(CONF_AUTH_URL, child->key) == 0)
							config_get_auth_params ( child, cb);
						else
							ERROR("%s plugin: Invalid configuration "
								  "option: %s.", PLUGIN_NAME, child->key);
				}
		} else
			ERROR("%s plugin: Invalid configuration "
			      "option: %s.", PLUGIN_NAME, ci->key);
	return;
}

static int wb_config_url (oconfig_item_t *ci){

#define CHECK_OPTIONAL_PARAM(str, name, section)			\
	if (!str)							\
		{							\
			INFO("%s plugin: There is no option  %s in section %s", PLUGIN_NAME, name, section); \
		}
#define CHECK_MANDATORY_PARAM(str, name)				\
	if (!str)							\
		{							\
			ERROR("%s plugin: Invalid configuration. There is no option %s", PLUGIN_NAME, name); \
			return -1;					\
		}

	wb_callback_t *cb;
	user_data_t user_data;

	cb = calloc (1, sizeof (*cb));
	if (cb == NULL){
		ERROR ("%s plugin: malloc failed.", PLUGIN_NAME);
		return (-1);
	}

	pthread_mutex_init (&cb->send_lock, /* attr = */ NULL);

	config_get_url_params (ci, cb);
	CHECK_OPTIONAL_PARAM(cb->auth_url, CONF_AUTH_URL, CONF_URL);
	CHECK_OPTIONAL_PARAM(cb->user,  CONF_AUTH_USER, CONF_AUTH_URL);
	CHECK_OPTIONAL_PARAM(cb->pass, CONF_AUTH_PASSORD, CONF_AUTH_URL);
	CHECK_OPTIONAL_PARAM(cb->tenantid, CONF_TENANTID, CONF_URL);
	CHECK_MANDATORY_PARAM(cb->url, CONF_URL);


	//!!2 отдельных транспорта
		/*Allocate CURL sending transport*/
		s_blueflood_transport = blueflood_curl_transport_alloc(cb->url, 
								       cb->auth_url, cb->user, cb->pass, cb->tenantid);
	if ( s_blueflood_transport == NULL ){
		ERROR ("%s plugin: construct transport error", PLUGIN_NAME );
		free_user_data(cb);
		return -1;
	}

	/*Allocate json generator*/
	if ( jsongen_init(&cb->yajl_gen) != 0 ){
		free_user_data(cb);
		return -1;
	}

	DEBUG ("%s plugin: Registering write callback with URL %s",
	       PLUGIN_NAME, cb->url);

	user_data.data = cb;

	/*set free_callback only once in according to plugin.c source code*/
	user_data.free_func = NULL;
	plugin_register_flush (PLUGIN_NAME, wb_flush, &user_data);
	/*register read plugin to ensure data will sent strictly by
	  configured timeout*/
	plugin_register_complex_read(NULL, PLUGIN_NAME, wb_read, NULL, &user_data);

	user_data.free_func = wb_callback_free;
	plugin_register_write (PLUGIN_NAME, wb_write, &user_data);

	INFO ("%s plugin: read/write callback registered", PLUGIN_NAME);

	return (0);
}

static int wb_config (oconfig_item_t *ci){
	INFO ("%s plugin: config callback", PLUGIN_NAME);
	int err=0;
	int i;
	for (i = 0; i < ci->children_num; i++) {
		oconfig_item_t *child = ci->children + i;
		//!!именовать отдельно от коллбеков
		if (strcasecmp (CONF_URL, child->key) == 0){
			if ((err=wb_config_url (child)) != 0){
				return err;
			}
		}
		else{
			ERROR ("%s plugin: Invalid configuration", PLUGIN_NAME);
			return -1;
		}
	}

	return 0;
}

static int wb_init (void){
	int curl_init_err;

	/* Call this while collectd is still single-threaded to avoid
	 * initialization issues in libgcrypt. */
	INFO ("%s plugin: init", PLUGIN_NAME);

	curl_init_err = blueflood_curl_transport_global_initialize(CURL_GLOBAL_SSL);
	if ( curl_init_err != 0 ){
		/*curl init error handling*/
		ERROR ("%s plugin: init error::curl_global_init=%d", PLUGIN_NAME, curl_init_err );
		return -1;
	}

	INFO ("%s plugin: init successful", PLUGIN_NAME);
	return (0);
}

static int wb_shutdown (void){
	INFO ("%s plugin: shutdown", PLUGIN_NAME);
	blueflood_curl_transport_global_finalize();
	INFO ("%s plugin: shutdown successful", PLUGIN_NAME);
	plugin_unregister_complex_config (PLUGIN_NAME);
	plugin_unregister_init (PLUGIN_NAME);
	plugin_unregister_read(PLUGIN_NAME);
	plugin_unregister_flush (PLUGIN_NAME);
	plugin_unregister_write (PLUGIN_NAME);
	plugin_unregister_shutdown (PLUGIN_NAME);

	return 0;
}

void module_register (void){
	INFO ("%s plugin: registered", PLUGIN_NAME);
	plugin_register_complex_config (PLUGIN_NAME, wb_config);
	plugin_register_init (PLUGIN_NAME, wb_init);
	plugin_register_shutdown (PLUGIN_NAME, wb_shutdown);
}

/* vim: set fdm=marker sw=8 ts=8 tw=78 et : */
