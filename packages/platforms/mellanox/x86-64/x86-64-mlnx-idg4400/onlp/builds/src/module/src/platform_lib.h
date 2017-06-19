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
#ifndef __PLATFORM_LIB_H__
#define __PLATFORM_LIB_H__

#include <onlp/fan.h>
#include <onlp/psu.h>
#include "x86_64_mlnx_idg4400_log.h"

// ./sm/infra/modules/AIM/module/inc/AIM/aim_log.h

#define CHASSIS_PSU_COUNT           2
#define CHASSIS_TOTAL_FAN_COUNT     10
#define CHASSIS_TOTAL_THERMAL_COUNT 13
#define CHASSIS_FAN_COUNT     (CHASSIS_TOTAL_FAN_COUNT - CHASSIS_PSU_COUNT)
#define CHASSIS_THERMAL_COUNT (CHASSIS_TOTAL_THERMAL_COUNT - CHASSIS_PSU_COUNT)

#define PSU1_ID 1
#define PSU2_ID 2

#define PSU_MODULE_PREFIX "/bsp/module/psu%d_%s"
#define PSU_POWER_PREFIX "/bsp/power/psu%d_%s"
#define IDPROM_PATH "/bsp/eeprom/%s%d_info"

typedef enum psu_type {
    PSU_TYPE_UNKNOWN,
    PSU_TYPE_AC_F2B,
    PSU_TYPE_AC_B2F
} psu_type_t;


/* LED related data
 */
enum onlp_led_id
{
    LED_RESERVED = 0,
    LED_SYSTEM,
    LED_FAN1,
    LED_FAN2,
    LED_FAN3,
    LED_FAN4,
    LED_PSU,
    LED_BAD_PORT,
    LED_UID,
};


/***************** Fans *****************/
#define FRONT_FANS_MIN_SPEED            6300
#define FRONT_FANS_MAX_SPEED            21000
#define REAR_FANS_MIN_SPEED             5400
#define REAR_FANS_MAX_SPEED             18000

extern const int min_fan_speed[CHASSIS_FAN_COUNT+1];
extern const int max_fan_speed[CHASSIS_FAN_COUNT+1];

#define IS_FAN_RPM_IN_THE_VALID_RANGE(_localId_, _rpm_) \
   ( ( ( min_fan_speed[ ( _localId_ ) ] * 0.87 ) < ( _rpm_ ) ) && ( ( _rpm_ ) < ( ( max_fan_speed[ ( _localId_ ) ] * 1.12 ) ) ) )


#define MIN_LIMIT_FRONT_FAN_RPM       FRONT_FANS_MIN_SPEED * 0.9  // 6300 -= 10 %
#define MIN_LIMIT_REAR_FAN_RPM        REAR_FANS_MIN_SPEED * 0.9 // 5400 -= 10 %


psu_type_t get_psu_type(int id, char* modelname, int modelname_len);

int psu_read_eeprom(int psu_index, onlp_psu_info_t* psu_info,
                     onlp_fan_info_t* fan_info);

int sfp_write_file(const char* fname, const char* data, int size);

int sfp_read_file(const char* fname, char* data, int size);

#endif  /* __PLATFORM_LIB_H__ */
