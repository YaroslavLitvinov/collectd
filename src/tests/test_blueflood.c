
#include <string.h>
#include <stdlib.h>

#include "plugin.h"
#include "common.h"
#include "liboconfig/oconfig.h"

#ifdef ENABLE_AUTH_CONFIG
# define TEST_INFINITE_DOUBLE
# define TEST_UNKNOWN_DATA_TYPE
#endif

extern void init_mock_test(int index);

/*in this test collectd mock & plugin linked statically */

struct callbacks_blueflood
{
	int (*plugin_config_cb) (oconfig_item_t *ci);
	int (*plugin_init_cb) (void);
	int (*plugin_shutdown_cb) (void);
	int (*plugin_write_cb) (const data_set_t *, const value_list_t *,
				user_data_t *);
	int (*plugin_flush_cb) (cdtime_t timeout, const char *identifier,
				user_data_t *);
	int (*plugin_read_cb) ();
	//data
	char *type_plugin_name;
	oconfig_item_t *config;
	user_data_t user_data;
	int temp_count_data_values;
};

struct callbacks_blueflood s_data;
pthread_t s_write_thread;
pthread_t s_write_thread2;

/*copied from collectD to get rid from linking yet another object file*/
int cf_util_get_string (const oconfig_item_t *ci, char **ret_string) /* {{{ */
{
	char *string;

	if ((ci->values_num != 1) || (ci->values[0].type != OCONFIG_TYPE_STRING))
	{
		ERROR ("cf_util_get_string: The %s option requires "
		       "exactly one string argument.", ci->key);
		return (-1);
	}

	string = strdup (ci->values[0].value.string);
	if (string == NULL)
		return (-1);

	if (*ret_string != NULL)
		sfree (*ret_string);
	*ret_string = string;

	return (0);
}
/*copied from collectD to get rid from linking yet another object file*/
int cf_util_get_int (const oconfig_item_t *ci, int *ret_value) /* {{{ */
{
	if ((ci == NULL) || (ret_value == NULL))
		return (EINVAL);

	if ((ci->values_num != 1) || (ci->values[0].type != OCONFIG_TYPE_NUMBER))
	{
		ERROR ("cf_util_get_int: The %s option requires "
		       "exactly one numeric argument.", ci->key);
		return (-1);
	}

	*ret_value = (int) ci->values[0].value.number;

	return (0);
} /* }}} int cf_util_get_int */


int format_name (char *ret, int ret_len,
		 const char *hostname,
		 const char *plugin, const char *plugin_instance,
		 const char *type, const char *type_instance)
{
	char *buffer;
	size_t buffer_size;

	buffer = ret;
	buffer_size = (size_t) ret_len;

#define APPEND(str) do {			\
		size_t l = strlen (str);	\
		if (l >= buffer_size)		\
			return (ENOBUFS);	\
		memcpy (buffer, (str), l);	\
		buffer += l; buffer_size -= l;	\
	} while (0)

	assert (plugin != NULL);
	assert (type != NULL);

	APPEND (hostname);
	APPEND ("/");
	APPEND (plugin);
	if ((plugin_instance != NULL) && (plugin_instance[0] != 0))
	{
		APPEND ("-");
		APPEND (plugin_instance);
	}
	APPEND ("/");
	APPEND (type);
	if ((type_instance != NULL) && (type_instance[0] != 0))
	{
		APPEND ("-");
		APPEND (type_instance);
	}
	assert (buffer_size > 0);
	buffer[0] = 0;

#undef APPEND
	return (0);
} /* int format_name */


/*collectD mockuped functions
********************************************/
int plugin_unregister_complex_config (const char *type)
{
	INFO ("plugin_unregister_complex_config");
	s_data.plugin_config_cb = NULL;
	return 0;
}

int plugin_unregister_init (const char *name)
{
	INFO ("plugin_unregister_init");
	s_data.plugin_init_cb = NULL;
	return 0;
}

int plugin_unregister_shutdown (const char *name)
{
	INFO ("plugin_unregister_shutdown");
	s_data.plugin_shutdown_cb = NULL;
	return 0;
}

int plugin_unregister_write (const char *name)
{
	INFO ("plugin_unregister_write");
	s_data.plugin_write_cb = NULL;
	return 0;
}

int plugin_unregister_flush (const char *name)
{
	INFO ("plugin_unregister_flush");
	s_data.plugin_flush_cb = NULL;
	return 0;
}

int plugin_unregister_read (const char *name)
{
	INFO ("plugin_unregister_read");
	s_data.plugin_read_cb = NULL;
	return 0;
}



oconfig_item_t *set_int_config_item(oconfig_item_t *config_item, const char *name, int value )
{
	config_item->key = strdup(name);
	config_item->values_num = 1;
	config_item->values = calloc(config_item->values_num, 
				     sizeof(oconfig_value_t) );
	config_item->values[0].type=OCONFIG_TYPE_NUMBER;
	config_item->values[0].value.number = value;
	return config_item;
}

oconfig_item_t *set_str_config_item(oconfig_item_t *config_item, const char *name, const char *value )
{
	config_item->key = strdup(name);
	config_item->values_num = 1;
	config_item->values = calloc(config_item->values_num, 
				     sizeof(oconfig_value_t) );
	config_item->values[0].type=OCONFIG_TYPE_STRING;
	config_item->values[0].value.string = strdup(value);
	return config_item;
}

oconfig_item_t *alloc_config_children(oconfig_item_t *parent_config, int children_num)
{
	parent_config->children_num = children_num;
	return parent_config->children 
		= calloc(parent_config->children_num, sizeof(oconfig_item_t) );
}



int plugin_register_complex_config (const char *type,
				    int (*callback) (oconfig_item_t *))
{
	oconfig_item_t *config;
	oconfig_item_t *nested_config;
#ifdef ENABLE_AUTH_CONFIG
	oconfig_item_t *nested_authconfig;
	int children_num = 3;
#else
	int children_num = 2;
#endif

	INFO ("plugin_register_complex_config");
	s_data.type_plugin_name = strdup(type);
	s_data.plugin_config_cb = callback;
	s_data.config = malloc(sizeof(oconfig_item_t));

	config = alloc_config_children(s_data.config, 1);
	set_str_config_item(config, "URL", "http://127.0.0.1:8000/");

	/*URL*/
	nested_config = alloc_config_children(config, children_num /*tenantdId, ttl, [Auth]*/);
	set_str_config_item(nested_config++, "TenantId", "987654321" );
	set_int_config_item(nested_config++, "ttlInSeconds", 12345 );
#ifdef ENABLE_AUTH_CONFIG

	/*AuthURL*/
	nested_authconfig = alloc_config_children(nested_config, 2 /*user, password*/);
	set_str_config_item(nested_config++, "AuthURL", "https://tokens");
	set_str_config_item(nested_authconfig++, "User", "foo");
	set_str_config_item(nested_authconfig++, "Password", "foo" );
#endif //ENABLE_AUTH_CONFIG
	return 0;
}

int plugin_register_init (const char *name, plugin_init_cb callback)
{
	INFO ("plugin_register_init");
	s_data.plugin_init_cb = callback;
	return 0;
}

int plugin_register_shutdown (const char *name,
			      plugin_shutdown_cb callback)
{
	INFO ("plugin_register_shutdown");
	s_data.plugin_shutdown_cb = callback;
	return 0;
}

int plugin_register_write (const char *name,
			   plugin_write_cb callback, user_data_t *user_data)
{
	INFO ("plugin_register_write");
	s_data.user_data = *user_data;
	s_data.plugin_write_cb = callback;
	return 0;
}

int plugin_register_flush (const char *name,
			   plugin_flush_cb callback, user_data_t *user_data)
{
	INFO ("plugin_register_flush");
	s_data.user_data = *user_data;
	s_data.plugin_flush_cb = callback;
	return 0;
}

int plugin_register_complex_read (const char *group, const char *name,
				  plugin_read_cb callback,
				  const struct timespec *interval,
				  user_data_t *user_data)
{
	INFO ("plugin_register_complex_read");
	s_data.user_data = *user_data;
	s_data.plugin_read_cb = callback;
	return 0;	
}

#include "write_blueflood.c"

/********************************************
collectD mockuped functions*/

void free_config_item_recursively(oconfig_item_t *config_item)
{
	if ( config_item != NULL )
	{
		int i=0;
		for(i=0; i < config_item->children_num; i++)
		{
			free(config_item->children[i].key);
			if (config_item->children[i].values[0].type == OCONFIG_TYPE_STRING)
			{
				free(config_item->children[i].values[0].value.string);
			}
			free(config_item->children[i].values);
			free_config_item_recursively(&config_item->children[i]);
		}
		free(config_item->children);
	}
}

void free_config()
{
	free_config_item_recursively(s_data.config);
	free(s_data.config), s_data.config = NULL;
	free(s_data.type_plugin_name), s_data.type_plugin_name = NULL;
}

void free_dataset(data_set_t *data_set, value_list_t *value_list)
{
	free(data_set->ds);
	free(value_list->values);
}

void fill_data_values_set(data_set_t *data_set, value_list_t *value_list, int count)
{
	int type, i;
	for (i=0; i < count; i++)
	{
		type = random() % 4; /*base types count*/
		if ( type == DS_TYPE_GAUGE){
			strncpy(data_set->ds[i].name, "gauge", 
				sizeof(data_set->ds[i].name));
			data_set->ds[i].type = type;
			value_list->values[i].gauge = 
#ifdef TEST_INFINITE_DOUBLE
				HUGE_VAL; //INFINITY
#else
			(double)random();
#endif //TEST_INFINITE_DOUBLE
		}
#ifdef TEST_UNKNOWN_DATA_TYPE
		else
		{
			data_set->ds[i].type = 100; //unknown data type
		}
#else
		else if ( type == DS_TYPE_COUNTER)
		{
			strncpy(data_set->ds[i].name, "counter", 
				sizeof(data_set->ds[i].name));
			data_set->ds[i].type = type;
			value_list->values[i].counter = random();
		}
		else if ( type == DS_TYPE_DERIVE)
		{
			strncpy(data_set->ds[i].name, "derive", 
				sizeof(data_set->ds[i].name));
			data_set->ds[i].type = type;
			value_list->values[i].derive = random();
		}
		else if ( type == DS_TYPE_ABSOLUTE)
		{
			strncpy(data_set->ds[i].name, "absolute", 
				sizeof(data_set->ds[i].name));
			data_set->ds[i].type = type;
			value_list->values[i].absolute = random();
		}
#endif //TEST_UNKNOWN_DATA_TYPE
	}
}

void *write_asynchronously(void *obj)
{
	struct callbacks_blueflood *data = (struct callbacks_blueflood *)obj;
	data_set_t data_set;
	int count = data->temp_count_data_values;

	memset(&data_set, '\0', sizeof(data_set_t));
	data_set.ds_num = count;
	/*TODO: figure out what dataset type means*/
	strncpy(data_set.type, "type", sizeof(data_set.type));
	data_set.ds = calloc(data_set.ds_num, sizeof(data_source_t));

	value_list_t value_list;
	memset(&value_list, '\0', sizeof(value_list_t));
	strncpy(value_list.host, "host", sizeof(value_list.host));
	strncpy(value_list.plugin, "plugin", sizeof(value_list.plugin));
	strncpy(value_list.type, "type", sizeof(value_list.type));
	value_list.values_len = count;
	value_list.time = time(NULL);
	value_list.interval = 1000000*30; //30sec
	value_list.values = malloc(sizeof(value_t)*count);

	fill_data_values_set(&data_set, &value_list, data->temp_count_data_values);
	data->plugin_write_cb( &data_set, &value_list, &data->user_data);
	free_dataset(&data_set, &value_list);
	return NULL;
}

#define CB_CONFIG_OK 0
#define CB_CONFIG_ERROR -1

#define CB_INIT_OK 0
#define CB_INIT_ERROR -1
/*use CB_INIT_SKIP if init callback return value will not be checked
  due to fail of prev test. really value itself have no sence and
  needed only for better source code readability*/
#define CB_INIT_SKIP -1 

void template_begin(char expected_config_result, char expected_init_result)
{
	memset(&s_data, '\0', sizeof(struct callbacks_blueflood));
	/*create plugin*/
	module_register();
	/*run config callback*/
	int config_callback_result = s_data.plugin_config_cb(s_data.config);
	assert(config_callback_result==expected_config_result);
	if ( config_callback_result != 0 ) return; 
	/*run init callback*/
	int init_callback_result = s_data.plugin_init_cb();
	assert(init_callback_result==expected_init_result);
}

void template_end()
{
	/*run free callback*/
	s_data.user_data.free_func(s_data.user_data.data);
	s_data.user_data.data = NULL;
	/*run shutdown callback*/
	s_data.plugin_shutdown_cb();
	/*free memories*/
	free_config();
}

void one_big_write();
void two_writes();
void two_hundred_writes();
void mock_test_0_construct_transport_error_curl_easy_init();
void mock_test_1_construct_transport_error_yajl_gen_alloc();
void mock_test_1_construct_transport_error_invalid_config();
void mock_test_1_construct_transport_error_invalid_config2();
void mock_test_2_init_callback_curl_global_init_error();
void mock_test_3_write_callback_yajl_gen_alloc_error_inside_read();
void mock_test_4_write_callback_curl_easy_perform_error();
void mock_test_5_write_callback_curl_easy_setopt_error();
void mock_test_6_all_ok();
void mock_test_7_auth();

int main()
{
	/*functional tests*/
#if 0
	one_big_write();
	two_writes();
	two_hundred_writes();
#endif
#ifndef ENABLE_AUTH_CONFIG
	/*tests without auth*/
	mock_test_0_construct_transport_error_curl_easy_init();
	mock_test_1_construct_transport_error_yajl_gen_alloc();
	mock_test_1_construct_transport_error_invalid_config();
	mock_test_1_construct_transport_error_invalid_config2();
	mock_test_2_init_callback_curl_global_init_error();
	mock_test_3_write_callback_yajl_gen_alloc_error_inside_read();
	mock_test_4_write_callback_curl_easy_perform_error();
	mock_test_5_write_callback_curl_easy_setopt_error();
	mock_test_6_all_ok();
#else
	/*tests with auth*/
	mock_test_7_auth();
#endif //ENABLE_AUTH_CONFIG
	return 0;
}

#if 0
void one_big_write()
{
	template_begin(CB_CONFIG_OK, CB_INIT_OK);
	/*test writes*/
	s_data.temp_count_data_values = 1000;
	int ret = pthread_create(&s_write_thread, NULL, write_asynchronously, &s_data);
	assert(0 == ret);
	ret = pthread_join(s_write_thread, NULL);
	assert(0 == ret);
	/*test flush*/
	s_data.plugin_flush_cb(0, "", &s_data.user_data);
	template_end();
}

void two_writes()
{
	template_begin(CB_CONFIG_OK, CB_INIT_OK);
	/*test writes*/
	s_data.temp_count_data_values = 4;
	int ret = pthread_create(&s_write_thread, NULL, write_asynchronously, &s_data);
	assert(0 == ret);
	int ret2 = pthread_create(&s_write_thread2, NULL, write_asynchronously, &s_data);
	assert(0 == ret2);
	ret = pthread_join(s_write_thread, NULL);
	assert(0 == ret);
	ret = pthread_join(s_write_thread2, NULL);
	assert(0 == ret2);

	/*test flush*/
	s_data.plugin_flush_cb(0, "", &s_data.user_data);
	template_end();
}

void two_hundred_writes()
{
	template_begin(CB_CONFIG_OK, CB_INIT_OK);
	int i;
	/*test writes*/
	s_data.temp_count_data_values = 10;
	for (i=0; i< 100; i++){
		int ret = pthread_create(&s_write_thread, NULL, write_asynchronously, &s_data);
		assert(0 == ret);
		int ret2 = pthread_create(&s_write_thread2, NULL, write_asynchronously, &s_data);
		assert(0 == ret2);
		ret = pthread_join(s_write_thread, NULL);
		assert(0 == ret);
		ret = pthread_join(s_write_thread2, NULL);
		assert(0 == ret2);
	}
	/*test flush*/
	s_data.plugin_flush_cb(0, "", &s_data.user_data);
	template_end();
}
#endif //0

void mock_test_0_construct_transport_error_curl_easy_init()
{
	init_mock_test(0);
	template_begin(CB_CONFIG_ERROR, CB_INIT_SKIP);
	free_config();
}

void mock_test_1_construct_transport_error_yajl_gen_alloc()
{
	init_mock_test(1);
	template_begin(CB_CONFIG_ERROR, CB_INIT_SKIP);
	free_config();
}

void mock_test_1_construct_transport_error_invalid_config()
{
	init_mock_test(1);
	memset(&s_data, '\0', sizeof(struct callbacks_blueflood));
	/*create plugin*/
	module_register();

	/*inject error as absent value*/
	free(s_data.config->children[0].key);
	s_data.config->children[0].key = strdup("");

	/*run config callback*/
	int config_callback_result = s_data.plugin_config_cb(s_data.config);
	assert(config_callback_result==-1);
	free_config();
}

void mock_test_1_construct_transport_error_invalid_config2()
{
	init_mock_test(1);
	memset(&s_data, '\0', sizeof(struct callbacks_blueflood));
	/*create plugin*/
	module_register();

	/*inject error as wrong key*/
	free(s_data.config->children[0].children[0].key);
	s_data.config->children[0].children[0].key = strdup("foo");

	/*run config callback*/
	int config_callback_result = s_data.plugin_config_cb(s_data.config);
	assert(config_callback_result==-1);
	free_config();
}

void mock_test_2_init_callback_curl_global_init_error()
{
	init_mock_test(2);
	template_begin(CB_CONFIG_OK, CB_INIT_ERROR);
	free_config();
}

void mock_test_3_write_callback_yajl_gen_alloc_error_inside_read()
{
	init_mock_test(3);
	template_begin(CB_CONFIG_OK, CB_INIT_OK);
	/*test writes*/
	s_data.temp_count_data_values = 4;
	write_asynchronously(&s_data);  /*just synchronous write into json buffer do not send*/
	/*inject yajl_gen_alloc error inside of write*/
	init_mock_test(1);
	int err = s_data.plugin_read_cb(&s_data.user_data);
	assert(err!=0);
	template_end();
}
void mock_test_4_write_callback_curl_easy_perform_error()
{
	init_mock_test(4);
	template_begin(CB_CONFIG_OK, CB_INIT_OK);
	/*test writes*/
	s_data.temp_count_data_values = 4;
	write_asynchronously(&s_data);  /*just synchronous write into json buffer do not send*/
	int err = s_data.plugin_read_cb(&s_data.user_data);
	assert(err!=0);
	template_end();
}

void mock_test_5_write_callback_curl_easy_setopt_error()
{
	init_mock_test(5);
	template_begin(CB_CONFIG_OK, CB_INIT_OK);
	/*test writes*/
	s_data.temp_count_data_values = 4;
	write_asynchronously(&s_data);  /*just synchronous write into json buffer do not send*/
	int err = s_data.plugin_read_cb(&s_data.user_data);
	assert(err!=0);
	template_end();
}

void mock_test_6_all_ok()
{
	init_mock_test(6);
	template_begin(CB_CONFIG_OK, CB_INIT_OK);
	/*test read callback*/
	s_data.temp_count_data_values = 4;
	write_asynchronously(&s_data);  /*just synchronous write into json buffer do not send*/
	int err = s_data.plugin_read_cb(&s_data.user_data);
	assert(err==0);
	/*test flush callback*/
	s_data.temp_count_data_values = 4;
	write_asynchronously(&s_data);  /*just synchronous write into json buffer do not send*/
	err = s_data.plugin_flush_cb(0, "", &s_data.user_data);
	assert(err==0);
	template_end();
}

void mock_test_7_auth()
{
	init_mock_test(7);
	template_begin(CB_CONFIG_OK, CB_INIT_OK);
	/*test read callback*/
	s_data.temp_count_data_values = 4;
	write_asynchronously(&s_data);  /*just synchronous write into json buffer do not send*/
	int err = s_data.plugin_read_cb(&s_data.user_data);
	assert(err==0);
	/*test flush callback*/
	s_data.temp_count_data_values = 4;
	write_asynchronously(&s_data);  /*just synchronous write into json buffer do not send*/
	err = s_data.plugin_flush_cb(0, "", &s_data.user_data);
	assert(err==0);
	template_end();
}
