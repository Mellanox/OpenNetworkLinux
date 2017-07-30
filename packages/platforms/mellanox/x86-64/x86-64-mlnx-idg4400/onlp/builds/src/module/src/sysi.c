/************************************************************
 * <bsn.cl fy=2014 v=onl>
 *
 *           Copyright 2014 Big Switch Networks, Inc.
 *
 * Licensed under the Eclipse Public License, Version 1.0 (the
 * "License"); you may not use this file except in compliance
 * with the License. You may obtain a copy of the License at
 *
 *        http://www.eclipse.org/legal/epl-v10.html
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND,
 * either express or implied. See the License for the specific
 * language governing permissions and limitations under the
 * License.
 *
 * </bsn.cl>
 ************************************************************
 *
 *
 *
 ***********************************************************/
#include <fcntl.h>
#include <stdio.h>
#include <ctype.h>
#include <syslog.h>
#include <unistd.h>
#include <sys/stat.h>
#include <onlplib/file.h>
#include <onlp/platformi/fani.h>
#include <onlp/platformi/ledi.h>
#include <onlp/platformi/psui.h>
#include <onlp/platformi/sysi.h>
#include <onlp/platformi/voltagei.h>
#include <onlp/platformi/thermali.h>
#include "platform_lib.h"
#include "x86_64_mlnx_idg4400_int.h"
#include "x86_64_mlnx_idg4400_log.h"

#define ONL_PLATFORM_NAME             "x86-64-mlnx-idg4400-r0"
#define ONIE_PLATFORM_NAME            "x86_64-mlnx_idg4400-r0"

#define NUM_OF_THERMAL_ON_MAIN_BROAD  CHASSIS_THERMAL_COUNT
#define NUM_OF_FAN_ON_MAIN_BROAD      CHASSIS_FAN_COUNT
#define NUM_OF_PSU_ON_MAIN_BROAD      2
#define NUM_OF_LED_ON_MAIN_BROAD      8

#define COMMAND_OUTPUT_BUFFER         256

#define PREFIX_PATH_ON_CPLD_DEV       "/bsp/cpld"
#define NUM_OF_CPLD                   3

#define MIN_FAN_SPEED                 60 /* TODO: Minimum fan speed should be dynamically updated according to the dynamic minimum fan speed table */

#define LOG_MAX_PRINT_LENGTH          1000

#define LOG_LEVEL_TO_STRING(_level) ( ( _level ) == LOG_EMERG ? "EMERGENCY": \
                                      ( _level ) == LOG_ALERT ? "ALERT": \
                                      ( _level ) == LOG_CRIT ? "CRITICAL": \
                                      ( _level ) == LOG_ERR ? "ERROR": \
                                      ( _level ) == LOG_WARNING ? "WARNING": \
                                      ( _level ) == LOG_NOTICE ? "NOTICE": \
                                      ( _level ) == LOG_DEBUG ? "DEBUG" : "INFO" )

extern int thermal_algorithm_enable;

extern char* onlp_thermal_sesnor_id_to_string(onlp_oid_t local_id);

static int fans_speed_changed_in_last_5_minutes = 0; /* Should initialized to 30 when changing the fan speed ( (60[sec] * 5[minutes]) / 10[secondes every period] )*/

static int onlp_sysi_previous_thermal_sensor_value[ THERMAL_ON_PSU2 + 1 ] = { 0 }; /* must map with onlp_thermal_id */

int onlp_sysi_is_system_in_shutdown_state( int *shutdown_state_bit );

static int thermal_algorithm_init_called = 0;

static char arr_cplddev_name[NUM_OF_CPLD][30] =
{
    "cpld_brd_version",
    "cpld_mgmt_version",
    "cpld_port_version"
};

void onlp_sysi_syslog_print(int syslog_level, const char *pref, const char *format, ... )
{
    char message[LOG_MAX_PRINT_LENGTH] = {'\0'};
    const char *level_s;
    va_list arg_list;
    va_start(arg_list,format);

    level_s = LOG_LEVEL_TO_STRING(syslog_level);

    vsprintf(message, format, arg_list);

    syslog(syslog_level, "%-40s | %-9s | %s", pref, level_s, message);

    va_end(arg_list);
}

const char*
onlp_sysi_platform_get(void)
{
    return ONL_PLATFORM_NAME;
}

int
onlp_sysi_platform_info_get(onlp_platform_info_t* pi)
{
    int   i, v[NUM_OF_CPLD]={0};

    for (i=0; i < NUM_OF_CPLD; i++) {
        v[i] = 0;
        if(onlp_file_read_int(v+i, "%s/%s", PREFIX_PATH_ON_CPLD_DEV, arr_cplddev_name[i]) < 0) {
            return ONLP_STATUS_E_INTERNAL;
        }
    }
    pi->cpld_versions = aim_fstrdup("brd=%d, mgmt=%d, port=%d", v[0], v[1], v[2]);

    return ONLP_STATUS_OK;
}

void
onlp_sysi_platform_info_free(onlp_platform_info_t* pi)
{
    aim_free(pi->cpld_versions);
}


int
onlp_sysi_oids_get(onlp_oid_t* table, int max)
{
    int i;
    onlp_oid_t* e = table;
    memset(table, 0, max*sizeof(onlp_oid_t));

    /* 8 Thermal sensors on the chassis */
    for (i = 1; i <= NUM_OF_THERMAL_ON_MAIN_BROAD; i++)
    {
        *e++ = ONLP_THERMAL_ID_CREATE(i);
    }

    /* 6 LEDs on the chassis */
    for (i = 1; i <= NUM_OF_LED_ON_MAIN_BROAD; i++)
    {
        *e++ = ONLP_LED_ID_CREATE(i);
    }

    /* 2 PSUs on the chassis */
    for (i = 1; i <= NUM_OF_PSU_ON_MAIN_BROAD; i++)
    {
        *e++ = ONLP_PSU_ID_CREATE(i);
    }

    /* 8 Fans and 2 PSU fans on the chassis */
    for (i = 1; i <= NUM_OF_FAN_ON_MAIN_BROAD; i++)
    {
        *e++ = ONLP_FAN_ID_CREATE(i);
    }

    return 0;
}

int
onlp_sysi_onie_info_get(onlp_onie_info_t* onie)
{
    int rv = onlp_onie_read_json(onie,
                                 "/lib/platform-config/current/onl/etc/onie/eeprom.json");
    if(rv >= 0) {
        if(onie->platform_name) {
            aim_free(onie->platform_name);
        }
        onie->platform_name = aim_strdup(ONIE_PLATFORM_NAME);
    }

    return rv;
}

int
onlp_sysi_platform_manage_leds(void)
{
	int rv = ONLP_STATUS_OK, volt_number, fan_number, thermal_number, psu_number, in_shutdown_state;
	onlp_psu_info_t pi;
	onlp_led_mode_t mode;
	enum onlp_led_id fan_led_id[4] = { LED_FAN1, LED_FAN2, LED_FAN3, LED_FAN4 };

	/*
	 * FAN Indicators
	 *
	 *     Green - Fan is operating
	 *     Red   - No power or Fan failure
	 *     Off   - No power
	 *
	 */
	for( fan_number = 1; fan_number<= 8; fan_number+=2)
	{
		/* each 2 fans had same led_fan */
		onlp_fan_info_t fi;
		/* check fan i */
		mode = ONLP_LED_MODE_GREEN;
		if(onlp_fani_info_get(ONLP_FAN_ID_CREATE(fan_number), &fi) < 0) {
			mode = ONLP_LED_MODE_RED;
		}
		else if( (fi.status & 0x1) == 0) {
			/* Not present */
			mode = ONLP_LED_MODE_RED;
		}
		else if(fi.status & ONLP_FAN_STATUS_FAILED) {
			mode = ONLP_LED_MODE_RED;
		}
		else
		{
			if( fi.rpm <  MIN_LIMIT_FRONT_FAN_RPM )
			{
				mode = ONLP_LED_MODE_RED;
			}
		}
		/* check fan i+1 */
		if(onlp_fani_info_get(ONLP_FAN_ID_CREATE(fan_number+1), &fi) < 0) {
			mode = ONLP_LED_MODE_RED;
		}
		else if( (fi.status & 0x1) == 0) {
			/* Not present */
			mode = ONLP_LED_MODE_RED;
		}
		else if(fi.status & ONLP_FAN_STATUS_FAILED) {
			mode = ONLP_LED_MODE_RED;
		}
		else
		{
			if( fi.rpm <  MIN_LIMIT_REAR_FAN_RPM )
			{
				mode = ONLP_LED_MODE_RED;
			}
		}
		onlp_ledi_mode_set(ONLP_OID_TYPE_CREATE(ONLP_OID_TYPE_LED,fan_led_id[fan_number/2]), mode);
	}

	/* PSU */
	mode = ONLP_LED_MODE_GREEN;
	for( psu_number=1; psu_number <=2; psu_number++)
	{
		if(onlp_psui_info_get(ONLP_PSU_ID_CREATE(psu_number), &pi) < 0) {
			mode = ONLP_LED_MODE_RED;
			break;
		}
		else if( (pi.status & 0x1) == 0) {
			/* Not present */
			mode = ONLP_LED_MODE_RED;
			break;
		}
		else if(pi.status & ONLP_PSU_STATUS_FAILED) {
			mode = ONLP_LED_MODE_RED;
			break;
		}
		else if(pi.status & ONLP_PSU_STATUS_UNPLUGGED) {
			mode = ONLP_LED_MODE_RED;
			break;
		}
	}

	onlp_ledi_mode_set(ONLP_OID_TYPE_CREATE(ONLP_OID_TYPE_LED,LED_PSU), mode);


    /*
     * Temperature Condition
     */
    rv = onlp_sysi_is_system_in_shutdown_state( &in_shutdown_state );
    if (rv < 0)
    {
        return rv;
    }
    if ( in_shutdown_state )
    {
        mode = ONLP_LED_MODE_RED;
    }
    else
    {
        mode = ONLP_LED_MODE_GREEN;
        for( thermal_number=THERMAL_CPU_CORE_0; thermal_number<= THERMAL_MNB; thermal_number++)
        {
            onlp_thermal_info_t ti;
            if( (onlp_thermali_info_get(ONLP_THERMAL_ID_CREATE(thermal_number), &ti) < 0) ||
                (ti.status & ONLP_THERMAL_STATUS_FAILED) ||
                ((ti.status & 0x1) == 0) )
            {
                mode = ONLP_LED_MODE_RED;
                break;
            }
            else {
                if( (ti.thresholds.shutdown > 0 ) && (ti.mcelsius > ti.thresholds.shutdown ) )
                {
                    mode = ONLP_LED_MODE_RED;
                    break;
                }
            }
        }
    }

    for( volt_number = 1; volt_number<= VOLTAGE_VMON_VOUT; volt_number++)
    {
        onlp_voltage_info_t fi;
        if(onlp_voltagei_info_get(ONLP_VOLTAGE_ID_CREATE(volt_number), &fi) < 0) {
            mode = ONLP_LED_MODE_RED;
        }
        else if( (fi.status & 0x1) == 0) {
            /* Not present */
            mode = ONLP_LED_MODE_RED;
        }
        else if(fi.status & ONLP_VOLTAGE_STATUS_FAILED) {
            mode = ONLP_LED_MODE_RED;
        }
        else
        {
            if( fi.range.min && fi.range.max)
            {
                if( (fi.voltage < fi.range.min ) || (fi.voltage > fi.range.max ) )
                {
                    mode = ONLP_LED_MODE_RED;
                }
            }
        }
    }
    onlp_ledi_mode_set(ONLP_OID_TYPE_CREATE(ONLP_OID_TYPE_LED,LED_SYSTEM), mode);

    return ONLP_STATUS_OK;
}

int onlp_sysi_store_current_thermal_values( int dest_thermal_values[] )
{
    int rv = ONLP_STATUS_OK, in_shutdown_state;
    onlp_thermal_info_t thermal_info;
    onlp_oid_t local_id;

    rv = onlp_sysi_is_system_in_shutdown_state( &in_shutdown_state );
    if (rv < 0)
    {
        onlp_sysi_syslog_print(LOG_ERR,
                               "onlp_sysi_store_current_thermal_values", "onlp_sysi_is_system_in_shutdown_state failed");
        return rv;
    }
    if ( !in_shutdown_state )
    {
        for ( local_id = THERMAL_CPU_CORE_0; local_id <= THERMAL_ON_PSU2; local_id++ )
        {
            rv = onlp_thermali_info_get( ONLP_THERMAL_ID_CREATE( local_id ), &thermal_info );
            if (rv < 0)
            {
                onlp_sysi_syslog_print(LOG_ERR,
                                       "onlp_sysi_store_current_thermal_values", "Failed to retrieve %s info",
                                       onlp_thermal_sesnor_id_to_string(local_id));
                dest_thermal_values[ local_id ] = 0;
                continue; /* TODO */
            }
            dest_thermal_values[ local_id ] = thermal_info.mcelsius;
        }
    }

    return ONLP_STATUS_OK;
}

int onlp_sysi_io_write_register(int base_addr, int offset, int len, int value)
{
    int   rv = 0;
    char  r_data[100]   = {0};

    snprintf(r_data, sizeof(r_data), "iorw -b %d -l%d -w -o %d -v %d", base_addr, len, offset, value);
    rv = system(r_data);
    if (rv == -1)
    {
        onlp_sysi_syslog_print(LOG_ERR,
                               "onlp_sysi_io_write_register", "Failed to write register %d, data %d via the command system(%s)",
                               (base_addr + offset), value, r_data);
        return ONLP_STATUS_E_INTERNAL;
    }

    return ONLP_STATUS_OK;
}

int onlp_sysi_io_read_register(int base_addr, int offset, int len, int *value)
{
    char  r_data[100]   = {0};
    char read_value[20] = {0};
    int read_value_counter = 0;
    int character;
    FILE *input_file;

    snprintf(r_data, sizeof(r_data), "iorw -b %d -l%d -r -o %d", base_addr, len, offset);
    input_file = popen(r_data, "r");

    while ( ( character = fgetc( input_file ) ) != EOF )
    {
        if ( character == '=' )
        {
            if ( ( character = fgetc( input_file ) ) == ' ' )
            {
                if ( ( character = fgetc( input_file ) ) == '0' )
                {
                    if ( ( character = fgetc( input_file ) ) == 'x' )
                    {
                        while ( ( ( character = fgetc( input_file ) ) != EOF ) && isxdigit( character ) )
                        {
                            read_value[read_value_counter] = character;
                            read_value_counter++;
                        }
                        break;
                    }
                }
            }
        }
    }
    pclose(input_file);

    *value = (int)strtol(read_value, NULL, 16);

    return ONLP_STATUS_OK;
}

int onlp_sysi_is_system_in_shutdown_state( int *shutdown_state_bit )
{
    int rv = ONLP_STATUS_OK;
    int register_value;
    rv = onlp_sysi_io_read_register(0x2500, 0x2e, 1, &register_value);
    if (rv < 0)
    {
        return rv;
    }

    *shutdown_state_bit = ( ( ~register_value ) & 0x4 ) >> 2;

    return ONLP_STATUS_OK;
}


/* Thermal algorithm control file parsing functions */

int onlp_sysi_string_to_float(char *str, float *output)
{
    int char_index = 0, devider = 1;
    float output_int = 0, output_float = 0;

    while ( ( str[char_index] != ' ' && str[char_index] != '\n' && str[char_index] != '\0' && str[char_index] != '.' && str[char_index] != EOF ) &&
            ( '0' <= str[char_index] ) && ( str[char_index] <= '9' ) )
    {
        output_int *= 10;
        output_int += ( str[char_index] - '0' );
        char_index++;
    }

    if ( str[char_index] == '.' )
    {
        char_index++;
        while ( ( str[char_index] != ' ' && str[char_index] != '\n' && str[char_index] != '\0' && str[char_index] != EOF) &&
                ( '0' <= str[char_index] ) && ( str[char_index] <= '9' ) )
        {
            output_float *= 10;
            output_float += ( str[char_index] - '0' );
            devider *= 10;
            char_index++;
        }
    }

    *output = output_int + ( output_float / devider );

    return ONLP_STATUS_OK;
}

int onlp_sysi_parse_cfg_file( char *filename, char out_cfg_keys[10][50], char out_cfg_data[10][50], unsigned int *out_keywords_number )
{
    FILE *input_file;
    int character;
    unsigned int  keyword_char_index = 0, data_char_index = 0, keywords_index = 0;

    input_file = fopen(filename, "rw+");
    if (input_file == 0)
    {
        onlp_sysi_syslog_print(LOG_ERR,
                               "onlp_sysi_parse_cfg_file", "fopen failed for file %s", filename);
        return ONLP_STATUS_E_INVALID;
    }

    while ( ( character = fgetc( input_file ) ) != EOF )
    {
        if ( character == '#')
        {
            while ( ( character = fgetc( input_file ) ) != EOF )
            {
                if ( character == '\n' || character == '\0' )
                {
                    break;
                }
            }
        }

        if ( character != ' ' && character != '\n' )
        {
            if ( character != '=' )
            {
                out_cfg_keys[keywords_index][keyword_char_index] = character;
                keyword_char_index++;
            }
            else
            {
                out_cfg_keys[keywords_index][keyword_char_index] = '\0';
                keyword_char_index = 0;
                while ( ( character = fgetc( input_file ) ) != EOF )
                {
                    if ( character == '#')
                    {
                        while ( ( character = fgetc( input_file ) ) != EOF )
                        {
                            if ( character == '\n' || character == '\0' )
                            {
                                break;
                            }
                        }
                    }
                    if ( character == '\n' || character == EOF )
                    {
                        out_cfg_data[keywords_index][data_char_index] = '\0';
                        data_char_index = 0;
                        keywords_index++;
                        break;
                    }
                    if ( character != ' ' )
                    {
                        out_cfg_data[keywords_index][data_char_index] = character;
                        data_char_index++;
                    }
                }
            }
        }
    }

    *out_keywords_number = keywords_index;

    fclose(input_file);

    return ONLP_STATUS_OK;

}


int onlp_sysi_thermal_algorithm_init( void )
{
    int rv = ONLP_STATUS_OK;
    float data;
    char cfg_keys[10][50] = {{'\0'}};
    char cfg_data[10][50] = {{'\0'}};
    unsigned int keywords_length, index;

    /* Parse the thermal algorithm control file */
    rv = onlp_sysi_parse_cfg_file( "/usr/src/local/mlnx/idg4400/.thermal_algorithm_control", cfg_keys, cfg_data, &keywords_length );
    if (rv < 0)
    {
        onlp_sysi_syslog_print(LOG_ERR,
                               "onlp_sysi_thermal_algorithm_init", "onlp_sysi_parse_cfg_file, enabling the thermal algorithm as default");
        thermal_algorithm_enable = 1;
    }
    else
    {
        for ( index = 0; index < keywords_length; index++ )
        {
            if ( strcmp(cfg_keys[index], "enable") == 0 )
            {
                onlp_sysi_string_to_float(cfg_data[index], &data);
                thermal_algorithm_enable = (int)data;
            }
        }
    }


    if ( thermal_algorithm_enable )
    {
        onlp_sysi_syslog_print(LOG_DEBUG,
                               "onlp_sysi_thermal_algorithm_init", "The Thermal Algorithm is enabled, initializing the watchdog...");
        /* Initialize the watchdog timer */
        rv = onlp_sysi_io_write_register(0x2500, 0xC9, 1, 14); /* Configure the watchdog timer to 2^14 = 16384 milliseconds */
        if (rv < 0)
        {
            return rv;
        }
        rv = onlp_sysi_io_write_register(0x2500, 0xCB, 1, 0x90); /* Bitmap: Configure the watchdog action to FULL_SPEED_FANS (bit 4), & the increment counter (bit 7) */
        if (rv < 0)
        {
            return rv;
        }
        rv = onlp_sysi_io_write_register(0x2500, 0xC8, 1, 0xEE); /* Bitmap: Configure the watchdog clear protective: bit 0 is WD1 timer clear & bit 4 is WD1 counter clear (1 = bit is write protected) */
        if (rv < 0)
        {
            return rv;
        }
        rv = onlp_sysi_io_write_register(0x2500, 0xC7, 1, 0x11); /* Bitmap: trigger the watchdog clear: bit 0 is WD1 timer clear & bit 4 is WD1 counter clear (1 = clear) */
        if (rv < 0)
        {
            return rv;
        }
        rv = onlp_sysi_store_current_thermal_values( onlp_sysi_previous_thermal_sensor_value );
        if (rv < 0)
        {
            return rv;
        }
    }

    return rv;
}

typedef struct onlp_sysi_thermal_range {
    /* low range limit temperature in milli-celsius */
    int low_limit;
    /* high range limit temperature in milli-celsius */
    int high_limit;
} onlp_sysi_thermal_range;

typedef struct onlp_sysi_thermal_sensor_ranges {
    onlp_sysi_thermal_range low_range;
    onlp_sysi_thermal_range in_range;
    onlp_sysi_thermal_range high_range;
    onlp_sysi_thermal_range very_high_range;
    onlp_sysi_thermal_range critical_range;
} onlp_sysi_thermal_sensor_ranges;

static const onlp_sysi_thermal_sensor_ranges thermal_sensors_ranges_values[ THERMAL_ON_PSU2 + 1 ] = /* must map with onlp_thermal_id */
{
        /* THERMAL_RESERVED */
        { { 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 } },
        /* THERMAL_CPU_CORE_0 */
        /* low_range  ,    in_range     ,   high_range    ,  very_high_range,   critical_range */
        { { 0, 60000 }, { 60000, 65000 }, { 65000, 70000 }, { 70000, 90000 }, { 90000, 105000 } },
        /* THERMAL_CPU_CORE_1 */
        { { 0, 60000 }, { 60000, 65000 }, { 65000, 70000 }, { 70000, 90000 }, { 90000, 105000 } },
        /* THERMAL_CPU_CORE_2 */
        { { 0, 60000 }, { 60000, 65000 }, { 65000, 70000 }, { 70000, 90000 }, { 90000, 105000 } },
        /* THERMAL_CPU_CORE_3 */
        { { 0, 60000 }, { 60000, 65000 }, { 65000, 70000 }, { 70000, 90000 }, { 90000, 105000 } },
        /* THERMAL_CPU_PACK */
        { { 0, 60000 }, { 60000, 65000 }, { 65000, 70000 }, { 70000, 90000 }, { 90000, 105000 } },
        /* THERMAL_FRONT */
        { { 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 } },
        /* THERMAL_REAR */
        { { 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 } },
        /* THERMAL_PEX */
        { { 0, 65000 }, { 65000, 70000 }, { 70000, 75000 }, { 75000, 95000 }, { 95000, 110000 } },
        /* THERMAL_NPS */
        { { 0, 60000 }, { 60000, 65000 }, { 65000, 70000 }, { 70000, 90000 }, { 90000, 105000 } },
        /* THERMAL_TCAM */
        { { 0, 50000 }, { 50000, 55000 }, { 55000, 60000 }, { 60000, 80000 }, { 80000, 95000 } },
        /* THERMAL_MNB */
        { { 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 } },
        /* THERMAL_ON_PSU1 */
        { { 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 } },
        /* THERMAL_ON_PSU2 */
        { { 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 } }
};

int onlp_sysi_update_fans_speed_percentage( int new_speed_percentage )
{
    int rv = ONLP_STATUS_OK, local_psu_fan_id;
    onlp_fan_info_t fan_info;

    rv = onlp_fani_percentage_set( ONLP_FAN_ID_CREATE(FAN_1_ON_MAIN_BOARD), new_speed_percentage );
    if (rv < 0)
    {
        onlp_sysi_syslog_print(LOG_ERR,
                               "onlp_sysi_update_fans_speed_percentage", "Failed to set fan %d speed to %d%%",
                               FAN_1_ON_MAIN_BOARD, new_speed_percentage);
        return rv;
    }

    /* Update PSU fans speed if needed */
    for ( local_psu_fan_id = FAN_1_ON_PSU1; local_psu_fan_id <= FAN_1_ON_PSU2; local_psu_fan_id++ )
    {
        rv = onlp_fani_info_get( ONLP_FAN_ID_CREATE( local_psu_fan_id ), &fan_info );
        if (rv < 0)
        {
            onlp_sysi_syslog_print(LOG_ERR,
                                   "onlp_sysi_update_fans_speed_percentage", "Failed to retrieve PSU-%d fan info",
                                   ( local_psu_fan_id - FAN_8_ON_MAIN_BOARD ));
            continue; /* TODO */
        }

        if ( fan_info.percentage < new_speed_percentage )
        {
            rv = onlp_fani_percentage_set( ONLP_FAN_ID_CREATE(local_psu_fan_id), new_speed_percentage );
            if (rv < 0)
            {
                onlp_sysi_syslog_print(LOG_ERR,
                                       "onlp_sysi_update_fans_speed_percentage", "Failed to set PSU-%d fan speed to %d%%",
                                       ( local_psu_fan_id - FAN_8_ON_MAIN_BOARD ), new_speed_percentage);
                return rv;
            }
        }
    }

    fans_speed_changed_in_last_5_minutes = 30;
    rv = onlp_sysi_store_current_thermal_values( onlp_sysi_previous_thermal_sensor_value );
    if (rv < 0)
    {
        onlp_sysi_syslog_print(LOG_ERR,
                               "onlp_sysi_update_fans_speed_percentage", "onlp_sysi_store_current_thermal_values failed");
        return rv;
    }

    onlp_sysi_syslog_print(LOG_INFO,
                           "onlp_sysi_update_fans_speed_percentage", "Fans speed was set to %d%%", new_speed_percentage);

    return ONLP_STATUS_OK;
}

int onlp_sysi_cold_algorithm( void )
{
    int rv = ONLP_STATUS_OK;
    onlp_fan_info_t fan_info;

    if ( fans_speed_changed_in_last_5_minutes == 0 )
    {
        rv = onlp_fani_info_get(ONLP_FAN_ID_CREATE(FAN_1_ON_MAIN_BOARD), &fan_info);
        if (rv < 0)
        {
            onlp_sysi_syslog_print(LOG_ERR,
                                   "onlp_sysi_cold_algorithm", "onlp_fani_info_get failed");
            return rv;
        }
        if ( MIN_FAN_SPEED < fan_info.percentage )
        {
            rv = onlp_sysi_update_fans_speed_percentage( fan_info.percentage * 0.9 );
            if (rv < 0)
            {
                onlp_sysi_syslog_print(LOG_ERR,
                                       "onlp_sysi_cold_algorithm", "onlp_sysi_update_fans_speed_percentage failed for speed %d",
                                       ( fan_info.percentage * 0.9 ));
                return rv;
            }
        }
        else
        {
            onlp_sysi_syslog_print(LOG_DEBUG,
                                   "onlp_sysi_cold_algorithm", "The fans are running at the minimum speed");
        }
    }

    return rv;
}

int onlp_sysi_hot_algorithm( )
{
    int rv = ONLP_STATUS_OK;
    onlp_fan_info_t fan_info;
    onlp_thermal_info_t thermal_info;
    onlp_oid_t local_id;
    int all_fans_speed_percentage, update_fans_speed = 0;

    rv = onlp_fani_info_get(ONLP_FAN_ID_CREATE(FAN_1_ON_MAIN_BOARD), &fan_info);
    if (rv < 0)
    {
        onlp_sysi_syslog_print(LOG_ERR,
                               "onlp_sysi_hot_algorithm", "onlp_fani_info_get failed");
        return rv;
    }
    if ( fan_info.percentage >= 100 )
    {
        return rv;
    }
    all_fans_speed_percentage = fan_info.percentage;

    for ( local_id = THERMAL_CPU_CORE_0; local_id <= THERMAL_ON_PSU2; local_id++ )
    {
        rv = onlp_thermali_info_get( ONLP_THERMAL_ID_CREATE( local_id ), &thermal_info );
        if (rv < 0)
        {
            onlp_sysi_syslog_print(LOG_ERR,
                                   "onlp_sysi_hot_algorithm", "Failed to retrieve %s info",
                                   onlp_thermal_sesnor_id_to_string(local_id));
            continue /* TODO */;
        }

        if ( ( thermal_info.mcelsius - onlp_sysi_previous_thermal_sensor_value[ local_id ] ) > 4000 )
        {
            onlp_sysi_syslog_print(LOG_NOTICE, "onlp_sysi_hot_algorithm", "%s temperature increased by more than 4000 [mcelsius] from the previous sample", onlp_thermal_sesnor_id_to_string(local_id));
            all_fans_speed_percentage *= 1.20;
            update_fans_speed = 1;
            break;
        }
    }

    if ( !update_fans_speed )
    {
        if ( fans_speed_changed_in_last_5_minutes == 0 )
        {
            all_fans_speed_percentage *= 1.10;
            update_fans_speed = 1;
        }
    }

    if ( update_fans_speed )
    {
        if ( all_fans_speed_percentage > 100 )
        {
            all_fans_speed_percentage = 100;
        }
        rv = onlp_sysi_update_fans_speed_percentage( all_fans_speed_percentage );
        if (rv < 0)
        {
            onlp_sysi_syslog_print(LOG_ERR,
                                   "onlp_sysi_hot_algorithm", "onlp_sysi_update_fans_speed_percentage failed for speed %d%%",
                                   all_fans_speed_percentage);
            return rv;
        }
    }

    return ONLP_STATUS_OK;
}

typedef enum onlp_sysi_thermal_range_status
{
    THERMAL_IN_LOW_RANGE = 0,
    THERMAL_IN_DESIRED_RANGE,
    THERMAL_IN_HIGH_RANGE,
    THERMAL_IN_VERY_HIGH_RANGE,
    THERMAL_IN_CRITICAL_RANGE,
} onlp_sysi_thermal_range_status;

int onlp_sysi_platform_manage_fans(void)
{
    int rv = ONLP_STATUS_OK;
    onlp_fan_info_t fan_info;
    onlp_thermal_info_t thermal_info;
    onlp_oid_t local_id, worst_thermal_sensor_local_id = THERMAL_CPU_CORE_0;
    int all_fans_speed_percentage = 60, update_fans_speed = 0, in_shutdown_state, current_fans_speed_percentage = 0, worst_thermal_sensor_temperature = 0;
    onlp_sysi_thermal_range_status all_thermals_status = THERMAL_IN_LOW_RANGE;

    openlog("ONLP - Thermal Algorithm: ", LOG_PID | LOG_CONS, LOG_USER);

    if ( !thermal_algorithm_init_called )
    {
        rv = onlp_sysi_thermal_algorithm_init();
        if (rv < 0)
        {
            onlp_sysi_syslog_print(LOG_ERR,
                                   "onlp_sysi_platform_manage_fans", "onlp_sysi_thermal_algorithm_init failed");
            return rv;
        }
        thermal_algorithm_init_called = 1;
    }
    if ( thermal_algorithm_enable )
    {
        /* reset the watchdog timer */
        onlp_sysi_syslog_print(LOG_DEBUG,
                               "onlp_sysi_platform_manage_fans", "Reset the watchdog timer");
        rv = onlp_sysi_io_write_register(0x2500, 0xC7, 1, 0x1); /* Bitmap: trigger the watchdog clear: bit 0 is WD1 timer clear (1 = clear) */
        if (rv < 0)
        {
            return rv;
        }

        if ( fans_speed_changed_in_last_5_minutes > 0 )
        {
            fans_speed_changed_in_last_5_minutes--;
        }

        rv = onlp_sysi_is_system_in_shutdown_state( &in_shutdown_state );
        if (rv < 0)
        {
            onlp_sysi_syslog_print(LOG_ERR,
                                   "onlp_sysi_platform_manage_fans", "onlp_sysi_is_system_in_shutdown_state failed");
            return rv;
        }
        if ( in_shutdown_state )
        {
            onlp_sysi_syslog_print(LOG_ALERT,
                                   "onlp_sysi_platform_manage_fans", "The system is in shut-down state! Power cycle must be done to recover the system (electric unplug for about 8 seconds and then replug)");
            /* In shutdown state just set fans speed to 100% */
            rv = onlp_sysi_update_fans_speed_percentage( 100 );
            if (rv < 0)
            {
                onlp_sysi_syslog_print(LOG_ERR,
                                       "onlp_sysi_platform_manage_fans", "onlp_sysi_update_fans_speed_percentage failed");
                return rv;
            }
        }
        else
        {
            /* Check the fans status */
            for ( local_id = FAN_1_ON_MAIN_BOARD; local_id <= FAN_8_ON_MAIN_BOARD; local_id++ )
            {
                rv = onlp_fani_info_get(ONLP_FAN_ID_CREATE(local_id), &fan_info);
                if (rv < 0)
                {
                    onlp_sysi_syslog_print(LOG_CRIT,
                                           "onlp_sysi_platform_manage_fans", "Failed to retrieve fan %d info",
                                           local_id);
                    return rv;
                }
                else
                {
                    if ( ! ( fan_info.status & 1 ) )
                    {
                        /* Fan is not present */
                        onlp_sysi_syslog_print(LOG_CRIT,
                                               "onlp_sysi_platform_manage_fans", "Fan %d is not present",
                                               local_id);
                        all_thermals_status = THERMAL_IN_CRITICAL_RANGE;
                        break;
                    }

                    if ( ! IS_FAN_RPM_IN_THE_VALID_RANGE( local_id, fan_info.rpm ) )
                    {
                        onlp_sysi_syslog_print(LOG_CRIT,
                                               "onlp_sysi_platform_manage_fans", "Fan %d RPM is not in the valid RPM range",
                                               local_id);
                        all_thermals_status = THERMAL_IN_CRITICAL_RANGE;
                        break;
                    }
                    current_fans_speed_percentage = fan_info.percentage;
                }
            }

            /* Check the thermal sensors status */
            for ( local_id = THERMAL_CPU_CORE_0; local_id <= THERMAL_ON_PSU2; local_id++ )
            {
                rv = onlp_thermali_info_get( ONLP_THERMAL_ID_CREATE( local_id ), &thermal_info );
                if (rv < 0)
                {
                    onlp_sysi_syslog_print(LOG_ERR,
                                           "onlp_sysi_platform_manage_fans", "Failed to retrieve %s info",
                                           onlp_thermal_sesnor_id_to_string(local_id));
                    continue; /* TODO */
                }


                if ( thermal_info.thresholds.shutdown > 0 )
                {
                    if ( thermal_info.mcelsius > thermal_info.thresholds.shutdown )
                    {
                        onlp_sysi_syslog_print(LOG_ALERT,
                                               "onlp_sysi_platform_manage_fans", "%s reached its shutdown threshold (its current temperature is %d [mcelsius], its shutdown threshold is %d [mcelsius]), shutting-down the system...",
                                               onlp_thermal_sesnor_id_to_string(local_id),
                                               thermal_info.mcelsius, thermal_info.thresholds.shutdown);
                        /* Shutdown the system TODO: Send SNMP trap */
                        rv = onlp_sysi_io_write_register(0x2500, 0x2f, 1, 0xfb);
                        if (rv < 0)
                        {
                            return rv;
                        }

                        rv = onlp_sysi_io_write_register(0x2500, 0x2e, 1, 0xfb);
                        if (rv < 0)
                        {
                            return rv;
                        }
                    }
                }
                if ( thermal_info.thresholds.warning > 0 )
                {
                    if ( thermal_info.mcelsius > thermal_info.thresholds.warning )
                    {
                        onlp_sysi_syslog_print(LOG_WARNING,
                                               "onlp_sysi_platform_manage_fans", "%s reached its warning threshold (its current temperature is %d [mcelsius], its warning threshold is %d [mcelsius])",
                                               onlp_thermal_sesnor_id_to_string(local_id),
                                               thermal_info.mcelsius, thermal_info.thresholds.warning);
                    }
                }

                if ( ( ( thermal_sensors_ranges_values[ local_id ].critical_range.high_limit - thermal_sensors_ranges_values[ local_id ].critical_range.low_limit ) > 0 ) &&
                          ( ( thermal_sensors_ranges_values[ local_id ].critical_range.low_limit < thermal_info.mcelsius ) && ( thermal_info.mcelsius < thermal_sensors_ranges_values[ local_id ].critical_range.high_limit ) ) )
                {
                    if ( all_thermals_status < THERMAL_IN_CRITICAL_RANGE )
                    {
                        all_thermals_status = THERMAL_IN_CRITICAL_RANGE;
                        worst_thermal_sensor_local_id = local_id;
                        worst_thermal_sensor_temperature = thermal_info.mcelsius;
                    }
                }
                else if ( ( ( thermal_sensors_ranges_values[ local_id ].very_high_range.high_limit - thermal_sensors_ranges_values[ local_id ].very_high_range.low_limit ) > 0 ) &&
                          ( ( thermal_sensors_ranges_values[ local_id ].very_high_range.low_limit < thermal_info.mcelsius ) && ( thermal_info.mcelsius < thermal_sensors_ranges_values[ local_id ].very_high_range.high_limit ) ) )
                {
                    if ( all_thermals_status < THERMAL_IN_VERY_HIGH_RANGE )
                    {
                        all_thermals_status = THERMAL_IN_VERY_HIGH_RANGE;
                        worst_thermal_sensor_local_id = local_id;
                        worst_thermal_sensor_temperature = thermal_info.mcelsius;
                    }
                }
                else if ( ( ( thermal_sensors_ranges_values[ local_id ].high_range.high_limit - thermal_sensors_ranges_values[ local_id ].high_range.low_limit ) > 0 ) &&
                          ( ( thermal_sensors_ranges_values[ local_id ].high_range.low_limit < thermal_info.mcelsius ) && ( thermal_info.mcelsius < thermal_sensors_ranges_values[ local_id ].high_range.high_limit ) ) )
                {
                    if ( all_thermals_status < THERMAL_IN_HIGH_RANGE )
                    {
                        all_thermals_status = THERMAL_IN_HIGH_RANGE;
                        worst_thermal_sensor_local_id = local_id;
                        worst_thermal_sensor_temperature = thermal_info.mcelsius;
                    }
                }
                else if ( ( ( thermal_sensors_ranges_values[ local_id ].in_range.high_limit - thermal_sensors_ranges_values[ local_id ].in_range.low_limit ) > 0 ) &&
                          ( ( thermal_sensors_ranges_values[ local_id ].in_range.low_limit < thermal_info.mcelsius ) && ( thermal_info.mcelsius < thermal_sensors_ranges_values[ local_id ].in_range.high_limit ) ) )
                {
                    if ( all_thermals_status < THERMAL_IN_DESIRED_RANGE )
                    {
                        all_thermals_status = THERMAL_IN_DESIRED_RANGE;
                        worst_thermal_sensor_local_id = local_id;
                        worst_thermal_sensor_temperature = thermal_info.mcelsius;
                    }
                }
                else if ( ( ( thermal_sensors_ranges_values[ local_id ].low_range.high_limit - thermal_sensors_ranges_values[ local_id ].low_range.low_limit ) > 0 ) &&
                          ( ( thermal_info.mcelsius < thermal_sensors_ranges_values[ local_id ].low_range.high_limit ) ) )
                {
                    if ( all_thermals_status < THERMAL_IN_LOW_RANGE )
                    {
                        all_thermals_status = THERMAL_IN_LOW_RANGE;
                        worst_thermal_sensor_local_id = local_id;
                        worst_thermal_sensor_temperature = thermal_info.mcelsius;
                    }
                }
            }

            switch ( all_thermals_status )
            {
                case THERMAL_IN_LOW_RANGE:
                    onlp_sysi_syslog_print(LOG_INFO, "onlp_sysi_platform_manage_fans", "System temperature is in the low range");
                    /* Do cold algorithm */
                    rv = onlp_sysi_cold_algorithm();
                    if (rv < 0)
                    {
                        return rv;
                    }
                    break;

                case THERMAL_IN_DESIRED_RANGE:
                    onlp_sysi_syslog_print(LOG_INFO, "onlp_sysi_platform_manage_fans", "System temperature is in the desired range");
                    /* do nothing */
                    break;

                case THERMAL_IN_HIGH_RANGE:
                    onlp_sysi_syslog_print(LOG_INFO, "onlp_sysi_platform_manage_fans", "System temperature is in the high range, the worst status is at %s, its current temperature is %d [mcelsius]", onlp_thermal_sesnor_id_to_string(worst_thermal_sensor_local_id), worst_thermal_sensor_temperature);
                    /* enforce minimum fan speed */
                    if ( current_fans_speed_percentage < MIN_FAN_SPEED )
                    {
                        all_fans_speed_percentage = MIN_FAN_SPEED;
                        update_fans_speed = 1;
                    }
                    break;

                case THERMAL_IN_VERY_HIGH_RANGE:
                    onlp_sysi_syslog_print(LOG_NOTICE, "onlp_sysi_platform_manage_fans", "System temperature is in the very high range, the worst status is at %s, its current temperature is %d [mcelsius]", onlp_thermal_sesnor_id_to_string(worst_thermal_sensor_local_id), worst_thermal_sensor_temperature);
                    /* Do hot algorithm */
                    rv = onlp_sysi_hot_algorithm();
                    if (rv < 0)
                    {
                        return rv;
                    }
                    break;

                case THERMAL_IN_CRITICAL_RANGE:
                    onlp_sysi_syslog_print(LOG_CRIT, "onlp_sysi_platform_manage_fans", "System temperature is in the critical range, the worst status is at %s, its current temperature is %d [mcelsius]", onlp_thermal_sesnor_id_to_string(worst_thermal_sensor_local_id), worst_thermal_sensor_temperature);
                    /* set fan speed to 100% */
                    if ( current_fans_speed_percentage < 100 )
                    {
                        all_fans_speed_percentage = 100;
                        update_fans_speed = 1;
                    }
                    break;
            }

            if ( update_fans_speed )
            {
                rv = onlp_sysi_update_fans_speed_percentage( all_fans_speed_percentage );
                if (rv < 0)
                {
                    onlp_sysi_syslog_print(LOG_ERR,
                                           "onlp_sysi_platform_manage_fans", "onlp_sysi_update_fans_speed_percentage failed");
                    return rv;
                }
            }
        }
    }
    else
    {
        onlp_sysi_syslog_print(LOG_INFO, "onlp_sysi_platform_manage_fans", "Theraml algorithm is disabled!");
    }

    closelog();
    return ONLP_STATUS_OK;
}
