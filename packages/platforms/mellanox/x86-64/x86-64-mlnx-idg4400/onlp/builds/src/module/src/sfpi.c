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
#include <onlp/platformi/sfpi.h>

#include <fcntl.h> /* For O_RDWR && open */
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <onlplib/file.h>
#include <onlplib/i2c.h>
#include <onlplib/sfp.h>
#include <sys/ioctl.h>
#include "platform_lib.h"

#define MAX_SFP_PATH                      64
#define SFP_SYSFS_INT_VALUE_LEN           20
static char sfp_node_path[MAX_SFP_PATH] = {0};
#define NUM_OF_SFP_PORTS                  12
#define SFP_EEPROM_FILE_SIZE              784 // 256 * 3 (each number is hex string + space) + 16 ('\n' char for each row)

/* Module type definitions - patches for 100GE BASED AOC and QSFP_PLUS A1 & A2 cables */
#define SFF_MODULE_TYPE_ADDR_0                           0
#define SFF_MODULE_TYPE_ADDR_128                         128
#define SFF_MODULE_TYPE_QSFP_PLUS_VAL                    0xd
#define SFF_MODULE_TYPE_QSFP_PLUS_OLD_VAL                0xe
#define SFF_MODULE_TYPE_QSFP28_VAL                       0x11
#define SFF_MODULE_COMPLIANCE_CODES_ADDR                 131
#define SFF_CC131_EXTENDED_BIT                           0x80
#define SFF_CC131_EXTENDED_ADDR                          192
#define SFF_CC192_100GE_AOC_TYPE_VAL                     0x01
#define SFF_EEPROM_CHECKSUM_ADDR                         191

static int
idg4400_sfp_node_read_int(char *node_path, int *value)
{
   int ret = 0, data_len;
   char buf[ SFP_SYSFS_INT_VALUE_LEN ];
   *value = 0;

   ret = onlp_file_read((uint8_t*)buf, sizeof(buf), &data_len, node_path);

   if (ret == 0) {
       *value = atoi(buf);
   }
   return ret;
}


static char*
idg4400_sfp_get_port_path(int port, char *node_name)
{
    sprintf(sfp_node_path, "/bsp/qsfp/qsfp%d/%s", port, node_name);
    return sfp_node_path;
}

/************************************************************
 *
 * SFPI Entry Points
 *
 ***********************************************************/
int
onlp_sfpi_init(void)
{
    return ONLP_STATUS_OK;
}

int
onlp_sfpi_bitmap_get(onlp_sfp_bitmap_t* bmap)
{
    /*
     * Ports {1, 12}
     */
    int p = 1;
    AIM_BITMAP_CLR_ALL(bmap);

    for (; p <= NUM_OF_SFP_PORTS; p++) {
        AIM_BITMAP_SET(bmap, p);
    }

    return ONLP_STATUS_OK;
}

int
onlp_sfpi_is_present(int port)
{
    /*
     * Return 1 if present.
     * Return 0 if not present.
     * Return < 0 if error.
     */
    int present = -1;
    char* path = idg4400_sfp_get_port_path(port, "present");

    if (idg4400_sfp_node_read_int(path, &present) != 0) {
        AIM_LOG_ERROR("Unable to read present status from port(%d)\r\n", port);
        return ONLP_STATUS_E_INTERNAL;
    }

    return present;
}

int
onlp_sfpi_presence_bitmap_get(onlp_sfp_bitmap_t* dst)
{
    int ii = 1;
    int rc = 0;

    for (;ii <= NUM_OF_SFP_PORTS; ii++) {
        rc = onlp_sfpi_is_present(ii);
        AIM_BITMAP_MOD(dst, ii, (1 == rc) ? 1 : 0);
    }

    return ONLP_STATUS_OK;
}

int
onlp_sfpi_eeprom_read(int port, uint8_t data[256])
{
    char* path = idg4400_sfp_get_port_path(port, "eeprom");
    char eeprom[SFP_EEPROM_FILE_SIZE];
    FILE *fp;
    char * pch;
    int idx;

    /*
     * Read the SFP eeprom into data[]
     *
     * Return MISSING if SFP is missing.
     * Return OK if eeprom is read
     */

    memset(data, 0, 256);

    fp= fopen(path, "r");
    if(fp == NULL)
    {
       AIM_LOG_ERROR("Unable to open eeprom file port(%d)\r\n", port);
       return -1;
    }

    idx = fread(eeprom, 1, SFP_EEPROM_FILE_SIZE, fp);
    if(idx != SFP_EEPROM_FILE_SIZE)
    {
       AIM_LOG_ERROR("Unable to read eeprom file port(%d)\r\n", port);
       return -1;
    }

    /* Splitting the file */
    idx = 0;
    pch = strtok (eeprom,"\n ");
    while (pch != NULL && idx < 256)
    {
      if(sscanf(pch, "%x" ,(unsigned int *)&data[idx]) != 1)
      {
         AIM_LOG_ERROR("Eeprom file is not valid, number %s is not hex, port(%d)\r\n",pch, port);
         return -1;
      }

      pch = strtok (NULL, "\n ");
      idx++;
    }

    if(idx < 256 )
    {
       AIM_LOG_ERROR("Eeprom file is not valid %s. number of bytes read %d, port(%d)\r\n",eeprom, idx, port);
       return -1;
    }

    /* Patch for 100GE AOC. When below conditions are TRUE, set module type to QSFP28:
     * - Module type is QSFP_PLUS 
     * - Extended_code bit is set
     * - Extended compliance code is 100GE_AOC type
     */
    if(data[SFF_MODULE_TYPE_ADDR_128] == SFF_MODULE_TYPE_QSFP_PLUS_VAL 
       && (data[SFF_MODULE_COMPLIANCE_CODES_ADDR] & SFF_CC131_EXTENDED_BIT )
       && (data[SFF_CC131_EXTENDED_ADDR] == SFF_CC192_100GE_AOC_TYPE_VAL ) )
    {
       data[SFF_MODULE_TYPE_ADDR_0] = SFF_MODULE_TYPE_QSFP28_VAL;
       data[SFF_MODULE_TYPE_ADDR_128] = SFF_MODULE_TYPE_QSFP28_VAL;
       /* Update eeprom checksum */
       data[SFF_EEPROM_CHECKSUM_ADDR] += (SFF_MODULE_TYPE_QSFP28_VAL - SFF_MODULE_TYPE_QSFP_PLUS_VAL);
    }

    /* Patch for QSFP_PLUS A1 & A2 cables. When below condition is TRUE, set module type to QSFP_PLUS:
     * - Module type on both identifiers (byte 0 and byte 128) is 0xe (SFF_MODULE_TYPE_QSFP_PLUS_OLD_VAL)
     */
    if(data[SFF_MODULE_TYPE_ADDR_128] == SFF_MODULE_TYPE_QSFP_PLUS_OLD_VAL 
       && (data[SFF_MODULE_TYPE_ADDR_0] == SFF_MODULE_TYPE_QSFP_PLUS_OLD_VAL ))
    {
       data[SFF_MODULE_TYPE_ADDR_0] = SFF_MODULE_TYPE_QSFP_PLUS_VAL;
       data[SFF_MODULE_TYPE_ADDR_128] = SFF_MODULE_TYPE_QSFP_PLUS_VAL;
       /* Update eeprom checksum */
       data[SFF_EEPROM_CHECKSUM_ADDR] += (SFF_MODULE_TYPE_QSFP_PLUS_VAL - SFF_MODULE_TYPE_QSFP_PLUS_OLD_VAL);
    }

    return ONLP_STATUS_OK;
}

int
onlp_sfpi_dev_readb(int port, uint8_t devaddr, uint8_t addr)
{
   uint8_t data;
   int size;
   int rv;
   char read_string[10] = {0};
   char* path = idg4400_sfp_get_port_path(port, "read");

   size = sprintf(read_string, "%x %x", addr, 1);

   rv = sfp_write_file(path, read_string, size);
   if(rv != ONLP_STATUS_OK) {
      AIM_LOG_ERROR("Unable to write qsfp file port(%d)\r\n", port);
      return rv;
   }

   /* Each byte is represented in two hex characters */
   memset(read_string, 0, sizeof(read_string));

   rv = sfp_read_file(path, read_string, 2);
   if(rv != ONLP_STATUS_OK) {
      AIM_LOG_ERROR("Unable to read qsfp file port(%d)\r\n", port);
      return rv;
   }

   if(sscanf(read_string, "%x" ,(unsigned int *)&data) != 1)
   {
      AIM_LOG_ERROR("Unable to read qsfp byte, read file format is invalid. port(%d)\r\n", port);
      return ONLP_STATUS_E_INTERNAL;
   }

   return data;
}

int
onlp_sfpi_dev_writeb(int port, uint8_t devaddr, uint8_t addr, uint8_t value)
{
   int size;
   int rv;
   char write_string[10] = {0};
   char* path = idg4400_sfp_get_port_path(port, "write");

   size = sprintf(write_string, "%x %x %x", addr, 1, value);

   rv = sfp_write_file(path, write_string, size);
   if(rv != ONLP_STATUS_OK) {
      AIM_LOG_ERROR("Unable to write qsfp file port(%d)\r\n", port);
      return rv;
   }

   return ONLP_STATUS_OK;
}

int
onlp_sfpi_dev_readw(int port, uint8_t devaddr, uint8_t addr)
{
    return ONLP_STATUS_E_UNSUPPORTED;
}

int
onlp_sfpi_dev_writew(int port, uint8_t devaddr, uint8_t addr, uint16_t value)
{
    return ONLP_STATUS_E_UNSUPPORTED;
}

int
onlp_sfpi_control_supported(int port, onlp_sfp_control_t control, int* rv)
{
    return ONLP_STATUS_E_UNSUPPORTED;
}

int
onlp_sfpi_control_set(int port, onlp_sfp_control_t control, int value)
{
	return ONLP_STATUS_E_UNSUPPORTED;
}

int
onlp_sfpi_control_get(int port, onlp_sfp_control_t control, int* value)
{
    return ONLP_STATUS_E_UNSUPPORTED;
}

int
onlp_sfpi_denit(void)
{
    return ONLP_STATUS_OK;
}

