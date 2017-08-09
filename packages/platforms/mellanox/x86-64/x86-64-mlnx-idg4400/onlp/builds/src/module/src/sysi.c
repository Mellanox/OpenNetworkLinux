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

extern int thermal_algorithm_enable;

static int fans_speed_changed_in_last_5_minutes = 0; /* Should initialized to 30 when changing the fan speed ( (60[sec] * 5[minutes]) / 10[secondes every period] )*/

static int onlp_sysi_previous_thermal_sensor_value[ THERMAL_ON_PSU2 + 1 ] = { 0 }; /* must map with onlp_thermal_id */

int onlp_sysi_is_system_in_shutdown_state( int *shutdown_state_bit );

static char arr_cplddev_name[NUM_OF_CPLD][30] =
{
    "cpld_brd_version",
    "cpld_mgmt_version",
    "cpld_port_version"
};

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
    int rv = ONLP_STATUS_OK;
    onlp_thermal_info_t thermal_info;
    onlp_oid_t local_id;

    for ( local_id = THERMAL_CPU_CORE_0; local_id <= THERMAL_ON_PSU2; local_id++ )
    {
        rv = onlp_thermali_info_get( ONLP_THERMAL_ID_CREATE( local_id ), &thermal_info );
        if (rv < 0)
        {
            dest_thermal_values[ local_id ] = 0;
            continue; /* TODO */
        }
        dest_thermal_values[ local_id ] = thermal_info.mcelsius;
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

int onlp_sysi_thermal_algorithm_init( void )
{
    /* TODO: Should read the thermal algorithm control file in this function.*/
    int rv = ONLP_STATUS_OK;
    rv = onlp_sysi_store_current_thermal_values( onlp_sysi_previous_thermal_sensor_value );
    if (rv < 0)
    {
        return rv;
    }

    if ( thermal_algorithm_enable )
    {
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
        return rv;
    }

    /* Update PSU fans speed if needed */
    for ( local_psu_fan_id = FAN_1_ON_PSU1; local_psu_fan_id <= FAN_1_ON_PSU2; local_psu_fan_id++ )
    {
        rv = onlp_fani_info_get( ONLP_FAN_ID_CREATE( local_psu_fan_id ), &fan_info );
        if (rv < 0)
        {
            continue; /* TODO */
        }

        if ( fan_info.percentage < new_speed_percentage )
        {
            rv = onlp_fani_percentage_set( ONLP_FAN_ID_CREATE(local_psu_fan_id), new_speed_percentage );
            if (rv < 0)
            {
                return rv;
            }
        }
    }

    fans_speed_changed_in_last_5_minutes = 30;
    rv = onlp_sysi_store_current_thermal_values( onlp_sysi_previous_thermal_sensor_value );
    if (rv < 0)
    {
        return rv;
    }

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
            return rv;
        }
        if ( MIN_FAN_SPEED < fan_info.percentage )
        {
            rv = onlp_sysi_update_fans_speed_percentage( fan_info.percentage * 0.9 );
            if (rv < 0)
            {
                return rv;
            }
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
            continue /* TODO */;
        }

        if ( ( thermal_info.mcelsius - onlp_sysi_previous_thermal_sensor_value[ local_id ] ) > 4000 )
        {
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
    onlp_oid_t local_id;
    int all_fans_speed_percentage = 60, update_fans_speed = 0, in_shutdown_state, current_fans_speed_percentage = 0;
    onlp_sysi_thermal_range_status all_thermals_status = THERMAL_IN_LOW_RANGE;

    if ( thermal_algorithm_enable )
    {
        /* reset the watchdog timer */
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
            return rv;
        }
        if ( in_shutdown_state )
        {
            /* In shutdown state just set fans speed to 100% */
            all_thermals_status = THERMAL_IN_CRITICAL_RANGE;
        }
        else
        {
            /* Check the fans status */
            for ( local_id = FAN_1_ON_MAIN_BOARD; local_id <= FAN_8_ON_MAIN_BOARD; local_id++ )
            {
                rv = onlp_fani_info_get(ONLP_FAN_ID_CREATE(local_id), &fan_info);
                if (rv < 0)
                {
                    return rv;
                }
                else
                {
                    if ( ! ( fan_info.status & 1 ) )
                    {
                        /* Fan is not present */
                        all_thermals_status = THERMAL_IN_CRITICAL_RANGE;
                        break;
                    }

                    if ( ! IS_FAN_RPM_IN_THE_VALID_RANGE( local_id, fan_info.rpm ) )
                    {
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
                    continue; /* TODO */
                }

                if ( thermal_info.thresholds.warning > 0 )
                {
                    if ( thermal_info.mcelsius > thermal_info.thresholds.warning )
                    {
                        printf("WARNING !!! thermal sensor %d reached its warning threshold\n", local_id);
                    }
                }

                if ( thermal_info.thresholds.shutdown > 0 )
                { /* TODO warn when reaching warning threshold */
                    /* TODO Add logging */
                    if ( thermal_info.mcelsius > thermal_info.thresholds.shutdown )
                    {
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

                if ( ( ( thermal_sensors_ranges_values[ local_id ].critical_range.high_limit - thermal_sensors_ranges_values[ local_id ].critical_range.low_limit ) > 0 ) &&
                          ( ( thermal_sensors_ranges_values[ local_id ].critical_range.low_limit < thermal_info.mcelsius ) && ( thermal_info.mcelsius < thermal_sensors_ranges_values[ local_id ].critical_range.high_limit ) ) )
                {
                    if ( all_thermals_status < THERMAL_IN_CRITICAL_RANGE )
                    {
                        all_thermals_status = THERMAL_IN_CRITICAL_RANGE;
                    }
                }
                else if ( ( ( thermal_sensors_ranges_values[ local_id ].very_high_range.high_limit - thermal_sensors_ranges_values[ local_id ].very_high_range.low_limit ) > 0 ) &&
                          ( ( thermal_sensors_ranges_values[ local_id ].very_high_range.low_limit < thermal_info.mcelsius ) && ( thermal_info.mcelsius < thermal_sensors_ranges_values[ local_id ].very_high_range.high_limit ) ) )
                {
                    if ( all_thermals_status < THERMAL_IN_VERY_HIGH_RANGE )
                    {
                        all_thermals_status = THERMAL_IN_VERY_HIGH_RANGE;
                    }
                }
                else if ( ( ( thermal_sensors_ranges_values[ local_id ].high_range.high_limit - thermal_sensors_ranges_values[ local_id ].high_range.low_limit ) > 0 ) &&
                          ( ( thermal_sensors_ranges_values[ local_id ].high_range.low_limit < thermal_info.mcelsius ) && ( thermal_info.mcelsius < thermal_sensors_ranges_values[ local_id ].high_range.high_limit ) ) )
                {
                    if ( all_thermals_status < THERMAL_IN_HIGH_RANGE )
                    {
                        all_thermals_status = THERMAL_IN_HIGH_RANGE;
                    }
                }
                else if ( ( ( thermal_sensors_ranges_values[ local_id ].in_range.high_limit - thermal_sensors_ranges_values[ local_id ].in_range.low_limit ) > 0 ) &&
                          ( ( thermal_sensors_ranges_values[ local_id ].in_range.low_limit < thermal_info.mcelsius ) && ( thermal_info.mcelsius < thermal_sensors_ranges_values[ local_id ].in_range.high_limit ) ) )
                {
                    if ( all_thermals_status < THERMAL_IN_DESIRED_RANGE )
                    {
                        all_thermals_status = THERMAL_IN_DESIRED_RANGE;
                    }
                }
                else if ( ( ( thermal_sensors_ranges_values[ local_id ].low_range.high_limit - thermal_sensors_ranges_values[ local_id ].low_range.low_limit ) > 0 ) &&
                          ( ( thermal_info.mcelsius < thermal_sensors_ranges_values[ local_id ].low_range.high_limit ) ) )
                {
                    if ( all_thermals_status < THERMAL_IN_LOW_RANGE )
                    {
                        all_thermals_status = THERMAL_IN_LOW_RANGE;
                    }
                }
            }
        }

        switch ( all_thermals_status )
        {
            case THERMAL_IN_LOW_RANGE:
                /* Do cold algorithm */
                rv = onlp_sysi_cold_algorithm();
                if (rv < 0)
                {
                    return rv;
                }
                break;

            case THERMAL_IN_DESIRED_RANGE:
                /* do nothing */
                break;

            case THERMAL_IN_HIGH_RANGE:
                /* enforce minimum fan speed */
                if ( current_fans_speed_percentage < MIN_FAN_SPEED )
                {
                    all_fans_speed_percentage = MIN_FAN_SPEED;
                    update_fans_speed = 1;
                }
                break;

            case THERMAL_IN_VERY_HIGH_RANGE:
                /* Do hot algorithm */
                rv = onlp_sysi_hot_algorithm();
                if (rv < 0)
                {
                    return rv;
                }
                break;

            case THERMAL_IN_CRITICAL_RANGE:
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
                return rv;
            }
        }
    }

    return ONLP_STATUS_OK;
}
