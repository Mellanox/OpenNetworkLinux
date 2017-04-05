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
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <AIM/aim.h>
#include <onlplib/file.h>
#include <sys/mman.h>
#include "platform_lib.h"

int
psu_read_eeprom(int psu_index, onlp_psu_info_t* psu_info, onlp_fan_info_t* fan_info)
{
    const char sanity_check[]   = "MLNX";
    const uint8_t serial_len    = 24;
    char data[256] = {0};
    bool sanity_found = false;
    int index = 0, rv = 0, len = 0;

    rv = onlp_file_read((uint8_t* )data, sizeof(data)-1, &len,
    		IDPROM_PATH, "psu", psu_index);
    if (rv < 0) {
        return ONLP_STATUS_E_INTERNAL;
    }

    /* Looking for sanity checker */
    while (index < sizeof(data) - sizeof(sanity_check) - 1) {
        if (!strncmp(&data[index], sanity_check, sizeof(sanity_check) - 1)) {
            sanity_found = true;
            break;
        }
        index++;
    }
    if (false == sanity_found) {
        return ONLP_STATUS_E_INVALID;
    }

    /* Serial number */
    index += strlen(sanity_check);
    if (psu_info) {
        strncpy(psu_info->serial, &data[index], sizeof(psu_info->serial));
    } else if (fan_info) {
        strncpy(fan_info->serial, &data[index], sizeof(fan_info->serial));
    }

    /* Part number */
    index += serial_len;
    if (psu_info) {
        strncpy(psu_info->model, &data[index], sizeof(psu_info->model));
    } else if (fan_info) {
        strncpy(fan_info->model, &data[index], sizeof(fan_info->model));
    }

    return ONLP_STATUS_OK;
}

int
sfp_write_file(const char* fname, const char* data, int size)
{
    int fd = open(fname, O_WRONLY);
    if (fd < 0) {
        return ONLP_STATUS_E_MISSING;
    }

    int nrd = write(fd, data, size);
    close(fd);

    if (nrd != size) {
        AIM_LOG_INTERNAL("Failed to write sfp file '%s'", fname);
        return ONLP_STATUS_E_INTERNAL;
    }

    return ONLP_STATUS_OK;
}


int
sfp_read_file(const char* fname, char* data, int size)
{
    int fd = open(fname, O_RDONLY);
    if (fd < 0) {
        return ONLP_STATUS_E_MISSING;
    }

    int nrd = read(fd, data, size);
    close(fd);

    if (nrd != size) {
        AIM_LOG_INTERNAL("Failed to read sfp file '%s'", fname);
        return ONLP_STATUS_E_INTERNAL;
    }
    return ONLP_STATUS_OK;
}
