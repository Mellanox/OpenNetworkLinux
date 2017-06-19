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
 * Fan Platform Implementation Defaults.
 *
 ***********************************************************/
#include <fcntl.h>
#include <onlplib/file.h>
#include <onlplib/mmap.h>
#include <onlp/platformi/fani.h>
#include "platform_lib.h"

#define PREFIX_PATH        "/bsp/fan/"
#define PREFIX_MODULE_PATH "/bsp/module/"

#define FAN_STATUS_OK  1

#define PERCENTAGE_MIN 60.0
#define PERCENTAGE_MAX 100.0
#define RPM_MAGIC_MIN  153.0
#define RPM_MAGIC_MAX  255.0

#define PSU_FAN_RPM_MIN 1000.0
#define PSU_FAN_RPM_MAX 18000.0

#define PROJECT_NAME
#define LEN_FILE_NAME 80

#define FAN_RESERVED        0
#define FAN_1_ON_MAIN_BOARD 1
#define FAN_2_ON_MAIN_BOARD 2
#define FAN_3_ON_MAIN_BOARD 3
#define FAN_4_ON_MAIN_BOARD 4
#define FAN_5_ON_MAIN_BOARD 5
#define FAN_6_ON_MAIN_BOARD 6
#define FAN_7_ON_MAIN_BOARD 7
#define FAN_8_ON_MAIN_BOARD 8
#define FAN_1_ON_PSU1       9
#define FAN_1_ON_PSU2       10


const int min_fan_speed[CHASSIS_FAN_COUNT+1] = { 0, /*      Front       ,       Rear */
                                                    FRONT_FANS_MIN_SPEED, REAR_FANS_MIN_SPEED,   /* Local fan id 1 & 2 */
                                                    FRONT_FANS_MIN_SPEED, REAR_FANS_MIN_SPEED,   /* Local fan id 3 & 4 */
                                                    FRONT_FANS_MIN_SPEED, REAR_FANS_MIN_SPEED,   /* Local fan id 5 & 6 */
                                                    FRONT_FANS_MIN_SPEED, REAR_FANS_MIN_SPEED }; /* Local fan id 7 & 8 */

const int max_fan_speed[CHASSIS_FAN_COUNT+1] = { 0, /*      Front       ,       Rear */
                                                    FRONT_FANS_MAX_SPEED, REAR_FANS_MAX_SPEED,   /* Local fan id 1 & 2 */
                                                    FRONT_FANS_MAX_SPEED, REAR_FANS_MAX_SPEED,   /* Local fan id 3 & 4 */
                                                    FRONT_FANS_MAX_SPEED, REAR_FANS_MAX_SPEED,   /* Local fan id 5 & 6 */
                                                    FRONT_FANS_MAX_SPEED, REAR_FANS_MAX_SPEED }; /* Local fan id 7 & 8 */

int thermal_algorithm_enable = 1;

typedef struct fan_path_S
{
    char status[LEN_FILE_NAME];
    char r_speed_get[LEN_FILE_NAME];
    char r_speed_set[LEN_FILE_NAME];
    char min[LEN_FILE_NAME];
    char max[LEN_FILE_NAME];
}fan_path_T;

#define _MAKE_FAN_PATH_ON_MAIN_BOARD(prj,id) \
    { #prj"fan"#id"_status", \
      #prj"fan"#id"_speed_get", \
      #prj"fan_speed_set", \
      #prj"fan"#id"_min", \
      #prj"fan"#id"_max" }

#define MAKE_FAN_PATH_ON_MAIN_BOARD(prj,id) _MAKE_FAN_PATH_ON_MAIN_BOARD(prj,id)

#define MAKE_FAN_PATH_ON_PSU(psu_id, fan_id) \
    {"psu"#psu_id"_status", \
     "psu"#psu_id"_fan"#fan_id"_speed_get", "", "", "",}

static fan_path_T fan_path[] =  /* must map with onlp_fan_id */
{
    MAKE_FAN_PATH_ON_MAIN_BOARD(PROJECT_NAME, FAN_RESERVED),
    MAKE_FAN_PATH_ON_MAIN_BOARD(PROJECT_NAME, FAN_1_ON_MAIN_BOARD),
    MAKE_FAN_PATH_ON_MAIN_BOARD(PROJECT_NAME, FAN_2_ON_MAIN_BOARD),
    MAKE_FAN_PATH_ON_MAIN_BOARD(PROJECT_NAME, FAN_3_ON_MAIN_BOARD),
    MAKE_FAN_PATH_ON_MAIN_BOARD(PROJECT_NAME, FAN_4_ON_MAIN_BOARD),
    MAKE_FAN_PATH_ON_MAIN_BOARD(PROJECT_NAME, FAN_5_ON_MAIN_BOARD),
    MAKE_FAN_PATH_ON_MAIN_BOARD(PROJECT_NAME, FAN_6_ON_MAIN_BOARD),
    MAKE_FAN_PATH_ON_MAIN_BOARD(PROJECT_NAME, FAN_7_ON_MAIN_BOARD),
    MAKE_FAN_PATH_ON_MAIN_BOARD(PROJECT_NAME, FAN_8_ON_MAIN_BOARD),
    MAKE_FAN_PATH_ON_PSU(1 ,1),
    MAKE_FAN_PATH_ON_PSU(2, 1)
};

#define MAKE_FAN_INFO_NODE_ON_MAIN_BOARD(id) \
    { \
        { ONLP_FAN_ID_CREATE(FAN_##id##_ON_MAIN_BOARD), "Chassis Fan "#id, 0 }, \
        0x0, \
        (ONLP_FAN_CAPS_SET_PERCENTAGE | ONLP_FAN_CAPS_GET_PERCENTAGE | \
         ONLP_FAN_CAPS_GET_RPM | ONLP_FAN_CAPS_SET_RPM), \
        0, \
        0, \
        ONLP_FAN_MODE_INVALID, \
    }

#define MAKE_FAN_INFO_NODE_ON_PSU(psu_id, fan_id) \
    { \
        { ONLP_FAN_ID_CREATE(FAN_##fan_id##_ON_PSU##psu_id), "Chassis PSU-"#psu_id" Fan "#fan_id, 0 }, \
        0x0, \
        (ONLP_FAN_CAPS_GET_RPM | ONLP_FAN_CAPS_GET_PERCENTAGE), \
        0, \
        0, \
        ONLP_FAN_MODE_INVALID, \
    }

/* Static fan information */
onlp_fan_info_t linfo[] = {
    { }, /* Not used */
    MAKE_FAN_INFO_NODE_ON_MAIN_BOARD(1),
    MAKE_FAN_INFO_NODE_ON_MAIN_BOARD(2),
    MAKE_FAN_INFO_NODE_ON_MAIN_BOARD(3),
    MAKE_FAN_INFO_NODE_ON_MAIN_BOARD(4),
    MAKE_FAN_INFO_NODE_ON_MAIN_BOARD(5),
    MAKE_FAN_INFO_NODE_ON_MAIN_BOARD(6),
    MAKE_FAN_INFO_NODE_ON_MAIN_BOARD(7),
    MAKE_FAN_INFO_NODE_ON_MAIN_BOARD(8),
    MAKE_FAN_INFO_NODE_ON_PSU(1,1),
    MAKE_FAN_INFO_NODE_ON_PSU(2,1)
};

#define VALIDATE(_id)                           \
    do {                                        \
        if(!ONLP_OID_IS_FAN(_id)) {             \
            return ONLP_STATUS_E_INVALID;       \
        }                                       \
    } while(0)

#define OPEN_READ_FILE(fullpath, data, nbytes, len)			\
	if (onlp_file_read((uint8_t*)data, nbytes, &len, fullpath) < 0)	\
       return ONLP_STATUS_E_INTERNAL;           \
	else													\
		AIM_LOG_VERBOSE("read data: %s\n", r_data);			\


static int
_onlp_fani_read_fan_eeprom(int local_id, onlp_fan_info_t* info)
{
    const char sanity_checker[] = "MLNX";
    const uint8_t sanity_offset = 8;
    const uint8_t sanity_len    = 4;
    const uint8_t block1_start  = 12;
    const uint8_t block1_type   = 1;
    const uint8_t block2_start  = 14;
    const uint8_t block2_type   = 5;
    const uint8_t serial_offset = 8;
    const uint8_t serial_len    = 24;
    const uint8_t part_len      = 20;
    const uint8_t fan_offset    = 14;
    const uint8_t multiplier    = 16;
    uint8_t data[256] = {0};
    uint8_t offset = 0;
    uint8_t temp   = 0;
    int rv  = 0;
    int len = 0;

    /* We have 4 FRU with 2 fans(total 8 fans).
       Eeprom is per FRU but not per fan.
       So, need to convert fan ID to FRU ID.*/
    if (local_id % 2) {
        local_id = local_id / 2 + 1;
    } else {
        local_id /= 2;
    }

    rv = onlp_file_read(data, sizeof(data), &len, IDPROM_PATH, "fan", local_id);
    if (rv < 0) {
        return ONLP_STATUS_E_INTERNAL;
    }

    /* Sanity checker */
    if (strncmp(sanity_checker, (char*)&data[sanity_offset], sanity_len)) {
        return ONLP_STATUS_E_INVALID;
    }

    /* Checking eeprom block type with S/N and P/N */
    if (data[block1_start + 1] != block1_type) {
        return ONLP_STATUS_E_INVALID;
    }

    /* Reading serial number */
    offset = data[block1_start] * multiplier + serial_offset;
    strncpy(info->serial, (char *)&data[offset], serial_len);

    /* Reading part number */
    offset += serial_len;
    strncpy(info->model, (char *)&data[offset], part_len);

    /* Reading fan direction */
    if (data[block2_start + 1] != block2_type) {
        return ONLP_STATUS_E_INVALID;
    }
    offset = data[block2_start] * multiplier + fan_offset;
    temp = data[offset];
    switch (temp) {
    case 1:
        info->caps |= ONLP_FAN_CAPS_F2B;
        break;
    case 2:
        info->caps |= ONLP_FAN_CAPS_B2F;
        break;
    default:
        break;
    }

    return ONLP_STATUS_OK;
}

static int
_onlp_fani_info_get_fan(int local_id, onlp_fan_info_t* info)
{
    int   len = 0, nbytes = 10;
    float percentage  = 0;
    float fru_index = 0;
    char  r_data[10]   = {0};
    char  fullpath[65] = {0};

    /* We have 4 FRU with 2 fans(total 8 fans).
       Eeprom is per FRU but not per fan.
       So, need to convert fan ID to FRU ID.*/
    if (local_id % 2) {
        fru_index = local_id / 2 + 1;
    } else {
        fru_index = local_id / 2;
    }

    /* get fan status
    */
    snprintf(fullpath, sizeof(fullpath), "%s%s", PREFIX_MODULE_PATH, fan_path[(int)fru_index].status);
    OPEN_READ_FILE(fullpath, r_data, nbytes, len);
    if (atoi(r_data) != FAN_STATUS_OK) {
        return ONLP_STATUS_OK;
    }
    info->status |= ONLP_FAN_STATUS_PRESENT;

     /* get fan speed
     */
    snprintf(fullpath, sizeof(fullpath), "%s%s", PREFIX_PATH, fan_path[local_id].r_speed_get);
    OPEN_READ_FILE(fullpath, r_data, nbytes, len);
    info->rpm = atoi(r_data);

    /* check failure */
    if (info->rpm <= 0) {
      info->status |= ONLP_FAN_STATUS_FAILED;
      return ONLP_STATUS_OK;
    }

    if (ONLP_FAN_CAPS_GET_PERCENTAGE & info->caps) {
        /* get speed percentage from rpm */
        if ( ( max_fan_speed[local_id] - min_fan_speed[local_id] ) > 0) {
            percentage = ( (float)info->rpm / max_fan_speed[local_id] ) * 100 ;
            info->percentage = (int)percentage;
        } else {
            return ONLP_STATUS_E_INTERNAL;
        }
    }

    return _onlp_fani_read_fan_eeprom(local_id, info);
}

static int
_onlp_fani_info_get_fan_on_psu(int local_id, int psu_id, onlp_fan_info_t* info)
{
    int   len = 0, nbytes = 10;
    char  r_data[10]   = {0};
    char  fullpath[80] = {0};
    float percentage = 0.0;

    /* get fan status
    */
    snprintf(fullpath, sizeof(fullpath), "%s%s", PREFIX_MODULE_PATH, fan_path[local_id].status);
    OPEN_READ_FILE(fullpath, r_data, nbytes, len);
    if (atoi(r_data) != FAN_STATUS_OK) {
        return ONLP_STATUS_OK;
    }
    info->status |= ONLP_FAN_STATUS_PRESENT;

    /* get fan speed
    */
    snprintf(fullpath, sizeof(fullpath), "%s%s", PREFIX_PATH, fan_path[local_id].r_speed_get);
    OPEN_READ_FILE(fullpath, r_data, nbytes, len);
    info->rpm = atoi(r_data);

    /* check failure */
    if (info->rpm <= 0) {
      info->status |= ONLP_FAN_STATUS_FAILED;
      return ONLP_STATUS_OK;
    }

    /* get speed percentage from rpm */
    percentage = ( (float)info->rpm / PSU_FAN_RPM_MAX ) * 100;
    info->percentage = (int)percentage;

    /* Serial number and model for PSU fan is the same as for appropriate PSU */
    if (FAN_1_ON_PSU1 == local_id) {
        if (0 != psu_read_eeprom(PSU1_ID, NULL, info))
            return ONLP_STATUS_E_INTERNAL;
    } else if (FAN_1_ON_PSU2 == local_id) {
        if (0 != psu_read_eeprom(PSU2_ID, NULL, info))
            return ONLP_STATUS_E_INTERNAL;
    }

    return ONLP_STATUS_OK;
}

int onlp_fani_string_to_float(char *str, float *output)
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

int onlp_fani_parse_cfg_file( char *filename, char out_cfg_keys[10][50], char out_cfg_data[10][50], unsigned int *out_keywords_number )
{
    FILE *input_file;
    int character;
    unsigned int  keyword_char_index = 0, data_char_index = 0, keywords_index = 0;

    input_file = fopen(filename, "rw+");
    if (input_file == 0)
    {
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


/*
 * This function will be called prior to all of onlp_fani_* functions.
 */
int
onlp_fani_init(void)
{
    int rv;
    float data;
    char cfg_keys[10][50] = {{'\0'}};
    char cfg_data[10][50] = {{'\0'}};
    unsigned int keywords_length, index;

    /* Parse the thermal algorithm control file */
    rv = onlp_fani_parse_cfg_file( "/usr/src/local/mlnx/idg4400/.thermal_algorithm_control", cfg_keys, cfg_data, &keywords_length );
    if (rv < 0) {
        thermal_algorithm_enable = 1;
    }
    else
    {
        for ( index = 0; index < keywords_length; index++ )
        {
            if ( strcmp(cfg_keys[index], "enable") == 0 )
            {
                onlp_fani_string_to_float(cfg_data[index], &data);
                thermal_algorithm_enable = (int)data;
            }
        }
    }
    return ONLP_STATUS_OK;
}

int
onlp_fani_info_get(onlp_oid_t id, onlp_fan_info_t* info)
{
    int rc = 0;
    int local_id = 0;
    VALIDATE(id);

    local_id = ONLP_OID_ID_GET(id);

    *info = linfo[local_id];

    switch (local_id)
        {
        case FAN_1_ON_PSU1:
            rc = _onlp_fani_info_get_fan_on_psu(local_id, PSU1_ID, info);
            break;
        case FAN_1_ON_PSU2:
            rc = _onlp_fani_info_get_fan_on_psu(local_id, PSU2_ID, info);
            break;
        case FAN_1_ON_MAIN_BOARD:
        case FAN_2_ON_MAIN_BOARD:
        case FAN_3_ON_MAIN_BOARD:
        case FAN_4_ON_MAIN_BOARD:
        case FAN_5_ON_MAIN_BOARD:
        case FAN_6_ON_MAIN_BOARD:
        case FAN_7_ON_MAIN_BOARD:
        case FAN_8_ON_MAIN_BOARD:
            rc =_onlp_fani_info_get_fan(local_id, info);
            break;
        default:
            rc = ONLP_STATUS_E_INVALID;
            break;
        }

    return rc;
}

/*
 * This function sets the speed of the given fan in RPM.
 *
 * This function will only be called if the fan supprots the RPM_SET
 * capability.
 *
 * It is optional if you have no fans at all with this feature.
 */
int
onlp_fani_rpm_set(onlp_oid_t id, int rpm)
{
    float pwm = 0.0;
    int   rv = 0, local_id = 0, nbytes = 10;
    char  r_data[10]   = {0};
    onlp_fan_info_t* info = NULL;

    VALIDATE(id);

    local_id = ONLP_OID_ID_GET(id);
    info = &linfo[local_id];

    if (0 == (ONLP_FAN_CAPS_SET_RPM & info->caps)) {
        return ONLP_STATUS_E_UNSUPPORTED;
    }

    /* reject rpm=0% (rpm=0%, stop fan) */
    if (0 == rpm) {
        return ONLP_STATUS_E_INVALID;
    }

    /* Set fan speed
       Converting percent to driver value.
       Driver accept value in range between 153 and 255.
       Value 153 is minimum rpm.
       Value 255 is maximum rpm.
    */
    if (local_id > sizeof(min_fan_speed)/sizeof(min_fan_speed[0])) {
        return ONLP_STATUS_E_INTERNAL;
    }
    if (max_fan_speed[local_id] - min_fan_speed[local_id] < 0) {
        return ONLP_STATUS_E_INTERNAL;
    }
    if (rpm < min_fan_speed[local_id] || rpm > max_fan_speed[local_id]) {
        return ONLP_STATUS_E_PARAM;
    }

    pwm = ( (float)rpm / max_fan_speed[local_id] ) * RPM_MAGIC_MAX;

    snprintf(r_data, sizeof(r_data), "%d", (int)pwm);
    nbytes = strnlen(r_data, sizeof(r_data));
    rv = onlp_file_write((uint8_t*)r_data, nbytes, "%s%s", PREFIX_PATH,
            fan_path[local_id].r_speed_set);
	if (rv < 0) {
		return ONLP_STATUS_E_INTERNAL;
	}

    return ONLP_STATUS_OK;
}

/*
 * This function sets the fan speed of the given OID as a percentage.
 *
 * This will only be called if the OID has the PERCENTAGE_SET
 * capability.
 *
 * It is optional if you have no fans at all with this feature.
 */
int
onlp_fani_percentage_set(onlp_oid_t id, int p)
{
    float pwm = 0.0;
    int   rv = 0, local_id = 0, nbytes = 10;
    char  r_data[10]   = {0};
    onlp_fan_info_t* info = NULL;

    VALIDATE(id);
    local_id = ONLP_OID_ID_GET(id);
    info = &linfo[local_id];

    if (0 == (ONLP_FAN_CAPS_SET_PERCENTAGE & info->caps)) {
        return ONLP_STATUS_E_UNSUPPORTED;
    }

    /* reject p=0% (p=0%, stop fan) */
    if (0 == p) {
        return ONLP_STATUS_E_INVALID;
    }

    if (p < PERCENTAGE_MIN || p > PERCENTAGE_MAX) {
        return ONLP_STATUS_E_PARAM;
    }

    /* Set fan speed
       Converting percent to driver value.
       Driver accept value in range between 153 and 255.
       Value 153 is 60%.
       Value 255 is 100%.
    */
    pwm = ( (float)p / 100 ) * RPM_MAGIC_MAX;
    snprintf(r_data, sizeof(r_data), "%d", (int)pwm);
    nbytes = strnlen(r_data, sizeof(r_data));
    rv = onlp_file_write((uint8_t*)r_data, nbytes, "%s%s", PREFIX_PATH,
            fan_path[local_id].r_speed_set);
    if (rv < 0) {
        return ONLP_STATUS_E_INTERNAL;
    }

    return ONLP_STATUS_OK;
}

/*
 * This function sets the fan speed of the given OID as per
 * the predefined ONLP fan speed modes: off, slow, normal, fast, max.
 *
 * Interpretation of these modes is up to the platform.
 *
 */
int
onlp_fani_mode_set(onlp_oid_t id, onlp_fan_mode_t mode)
{
    return ONLP_STATUS_E_UNSUPPORTED;
}

/*
 * This function sets the fan direction of the given OID.
 *
 * This function is only relevant if the fan OID supports both direction
 * capabilities.
 *
 * This function is optional unless the functionality is available.
 */
int
onlp_fani_dir_set(onlp_oid_t id, onlp_fan_dir_t dir)
{
    return ONLP_STATUS_E_UNSUPPORTED;
}

/*
 * Generic fan ioctl. Optional.
 */
int
onlp_fani_ioctl(onlp_oid_t id, va_list vargs)
{
    return ONLP_STATUS_E_UNSUPPORTED;
}

