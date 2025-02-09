/******************************************************************************
 *
 *  Copyright (C) 2014, The linux Foundation. All rights reserved.
 *
 *  Not a Contribution.
 *
 *  Copyright (C) 2009-2012 Broadcom Corporation
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at:
 *
 *  http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 ******************************************************************************/

/************************************************************************************
 *
 *  Filename:      rfcommtest.c
 *
 *  Description:   RFCOMM Test application
 *
 ***********************************************************************************/

#include <stdio.h>
#include <dlfcn.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <ctype.h>
#include <fcntl.h>
#include <sys/prctl.h>
#include <sys/capability.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netdb.h>
#include <private/android_filesystem_config.h>
#include <android/log.h>
#include <hardware/hardware.h>
#include <hardware/bluetooth.h>
#include <hardware/vendor.h>

#include "bt_target.h"
#include "bt_testapp.h"
#include "raw_address.h"

/************************************************************************************
**  Constants & Macros
************************************************************************************/

#define PID_FILE "/data/.bdt_pid"

#ifndef MAX
#define MAX(x, y) ((x) > (y) ? (x) : (y))
#endif

#define CASE_RETURN_STR(const) case const: return #const;
#define DISCONNECT 3

/************************************************************************************
**  Local type definitions
************************************************************************************/

/************************************************************************************
**  Static variables
************************************************************************************/

static unsigned char main_done = 0;
static bt_status_t status;

/* Main API */
const bt_interface_t* sBtInterface = NULL;
const btvendor_interface_t *btvendorInterface  = NULL;
const btrfcomm_interface_t *sRfcInterface = NULL;

static gid_t groups[] = { AID_NET_BT, AID_INET, AID_NET_BT_ADMIN,
                          AID_SYSTEM, AID_MISC, AID_SDCARD_RW,
                          AID_NET_ADMIN, AID_VPN};

/* Set to 1 when the Bluedroid stack is enabled */
static unsigned char bt_enabled = 0;
static bool strict_mode = FALSE;

/************************************************************************************
**  Static functions
************************************************************************************/

static void process_cmd(char *p, unsigned char is_job);
static void bdt_log(const char *fmt_str, ...);


/************************************************************************************
**  Externs
************************************************************************************/

/************************************************************************************
**  Functions
************************************************************************************/


/************************************************************************************
**  Shutdown helper functions
************************************************************************************/

static void bdt_shutdown(void)
{
    bdt_log("shutdown bdroid test app\n");
    main_done = 1;
}


/*****************************************************************************
** Android's init.rc does not yet support applying linux capabilities
*****************************************************************************/

static void config_permissions(void)
{
    struct __user_cap_header_struct header;
    struct __user_cap_data_struct cap[2];

    bdt_log("set_aid_and_cap : pid %d, uid %d gid %d", getpid(), getuid(), getgid());

    header.pid = 0;

    prctl(PR_SET_KEEPCAPS, 1, 0, 0, 0);

    setuid(AID_BLUETOOTH);
    setgid(AID_BLUETOOTH);

    header.version = _LINUX_CAPABILITY_VERSION_3;

    cap[CAP_TO_INDEX(CAP_NET_RAW)].permitted |= CAP_TO_MASK(CAP_NET_RAW);
    cap[CAP_TO_INDEX(CAP_NET_ADMIN)].permitted |= CAP_TO_MASK(CAP_NET_ADMIN);
    cap[CAP_TO_INDEX(CAP_NET_BIND_SERVICE)].permitted |= CAP_TO_MASK(CAP_NET_BIND_SERVICE);
    cap[CAP_TO_INDEX(CAP_SYS_RAWIO)].permitted |= CAP_TO_MASK(CAP_SYS_RAWIO);
    cap[CAP_TO_INDEX(CAP_SYS_NICE)].permitted |= CAP_TO_MASK(CAP_SYS_NICE);
    cap[CAP_TO_INDEX(CAP_SETGID)].permitted |= CAP_TO_MASK(CAP_SETGID);
    cap[CAP_TO_INDEX(CAP_WAKE_ALARM)].permitted |= CAP_TO_MASK(CAP_WAKE_ALARM);

    cap[CAP_TO_INDEX(CAP_NET_RAW)].effective |= CAP_TO_MASK(CAP_NET_RAW);
    cap[CAP_TO_INDEX(CAP_NET_ADMIN)].effective |= CAP_TO_MASK(CAP_NET_ADMIN);
    cap[CAP_TO_INDEX(CAP_NET_BIND_SERVICE)].effective |= CAP_TO_MASK(CAP_NET_BIND_SERVICE);
    cap[CAP_TO_INDEX(CAP_SYS_RAWIO)].effective |= CAP_TO_MASK(CAP_SYS_RAWIO);
    cap[CAP_TO_INDEX(CAP_SYS_NICE)].effective |= CAP_TO_MASK(CAP_SYS_NICE);
    cap[CAP_TO_INDEX(CAP_SETGID)].effective |= CAP_TO_MASK(CAP_SETGID);
    cap[CAP_TO_INDEX(CAP_WAKE_ALARM)].effective |= CAP_TO_MASK(CAP_WAKE_ALARM);

    capset(&header, &cap[0]);
    setgroups(sizeof(groups)/sizeof(groups[0]), groups);
}
#if 0
/*Pin_Request_cb */
static void pin_remote_request_callback (RawAddress *remote_bd_addr,
                             bt_bdname_t *bd_name, uint32_t cod)
{
    bt_pin_code_t    pin_code;
    /* Default 1234 is the Pin code */
    pin_code.pin[0]  =  0x31;
    pin_code.pin[1]  =  0x32;
    pin_code.pin[2]  =  0x33;
    pin_code.pin[3]  =  0x34;

    bdt_log("bdt PIN Remote Request");
    sBtInterface->pin_reply(remote_bd_addr ,1 , 4 , &pin_code);
}
#endif

/* Pairing in Case of SSP */
static void ssp_remote_requst_callback(RawAddress *remote_bd_addr, bt_bdname_t *bd_name,
                             uint32_t cod, bt_ssp_variant_t pairing_variant, uint32_t pass_key)
{
    if (pairing_variant == BT_SSP_VARIANT_PASSKEY_ENTRY)
    {
        bdt_log("bdt ssp remote request not supported");
        return;
    }
    bdt_log("bdt accept SSP pairing");
    sBtInterface->ssp_reply(remote_bd_addr,  pairing_variant, 1,  pass_key);
}




/*****************************************************************************
**   Logger API
*****************************************************************************/

void bdt_log(const char *fmt_str, ...)
{
    static char buffer[1024];
    va_list ap;

    va_start(ap, fmt_str);
    vsnprintf(buffer, 1024, fmt_str, ap);
    va_end(ap);

    fprintf(stdout, "%s\n", buffer);
}

/*******************************************************************************
 ** Misc helper functions
 *******************************************************************************/
static const char* dump_bt_status(bt_status_t status)
{
    switch(status)
    {
        CASE_RETURN_STR(BT_STATUS_SUCCESS)
        CASE_RETURN_STR(BT_STATUS_FAIL)
        CASE_RETURN_STR(BT_STATUS_NOT_READY)
        CASE_RETURN_STR(BT_STATUS_NOMEM)
        CASE_RETURN_STR(BT_STATUS_BUSY)
        CASE_RETURN_STR(BT_STATUS_UNSUPPORTED)

        default:
            return "unknown status code";
    }
}
#if 0
static void hex_dump(char *msg, void *data, int size, int trunc)
{
    unsigned char *p = data;
    unsigned char c;
    int n;
    char bytestr[4] = {0};
    char addrstr[10] = {0};
    char hexstr[ 16*3 + 5] = {0};
    char charstr[16*1 + 5] = {0};

    bdt_log("%s  \n", msg);

    /* truncate */
    if(trunc && (size>32))
        size = 32;

    for(n=1;n<=size;n++) {
        if (n%16 == 1) {
            /* store address for this line */
            snprintf(addrstr, sizeof(addrstr), "%.4x",
               (unsigned int)((uintptr_t)p-(uintptr_t)data) );
        }

        c = *p;
        if (isalnum(c) == 0) {
            c = '.';
        }

        /* store hex str (for left side) */
        snprintf(bytestr, sizeof(bytestr), "%02X ", *p);
        strncat(hexstr, bytestr, sizeof(hexstr)-strlen(hexstr)-1);

        /* store char str (for right side) */
        snprintf(bytestr, sizeof(bytestr), "%c", c);
        strncat(charstr, bytestr, sizeof(charstr)-strlen(charstr)-1);

        if(n%16 == 0) {
            /* line completed */
            bdt_log("[%4.4s]   %-50.50s  %s\n", addrstr, hexstr, charstr);
            hexstr[0] = 0;
            charstr[0] = 0;
        } else if(n%8 == 0) {
            /* half line: add whitespaces */
            strncat(hexstr, "  ", sizeof(hexstr)-strlen(hexstr)-1);
            strncat(charstr, " ", sizeof(charstr)-strlen(charstr)-1);
        }
        p++; /* next byte */
    }

    if (strlen(hexstr) > 0) {
        /* print rest of buffer if not empty */
        bdt_log("[%4.4s]   %-50.50s  %s\n", addrstr, hexstr, charstr);
    }
}
#endif
/*******************************************************************************
 ** Console helper functions
 *******************************************************************************/

void skip_blanks(char **p)
{
  while (**p == ' ')
    (*p)++;
}

uint32_t get_int(char **p, int DefaultValue)
{
  uint32_t Value = 0;
  unsigned char   UseDefault;

  UseDefault = 1;
  skip_blanks(p);

  while ( ((**p)<= '9' && (**p)>= '0') )
    {
      Value = Value * 10 + (**p) - '0';
      UseDefault = 0;
      (*p)++;
    }

  if (UseDefault)
    return DefaultValue;
  else
    return Value;
}

int get_signed_int(char **p, int DefaultValue)
{
  int    Value = 0;
  unsigned char   UseDefault;
  unsigned char  NegativeNum = 0;

  UseDefault = 1;
  skip_blanks(p);

  if ( (**p) == '-')
    {
      NegativeNum = 1;
      (*p)++;
    }
  while ( ((**p)<= '9' && (**p)>= '0') )
    {
      Value = Value * 10 + (**p) - '0';
      UseDefault = 0;
      (*p)++;
    }

  if (UseDefault)
    return DefaultValue;
  else
    return ((NegativeNum == 0)? Value : -Value);
}

void get_str(char **p, char *Buffer)
{
  skip_blanks(p);

  while (**p != 0 && **p != ' ')
    {
      *Buffer = **p;
      (*p)++;
      Buffer++;
    }

  *Buffer = 0;
}

uint32_t get_hex(char **p, int DefaultValue)
{
  uint32_t Value = 0;
  unsigned char   UseDefault;

  UseDefault = 1;
  skip_blanks(p);

  while ( ((**p)<= '9' && (**p)>= '0') ||
          ((**p)<= 'f' && (**p)>= 'a') ||
          ((**p)<= 'F' && (**p)>= 'A') )
    {
      if (**p >= 'a')
        Value = Value * 16 + (**p) - 'a' + 10;
      else if (**p >= 'A')
        Value = Value * 16 + (**p) - 'A' + 10;
      else
        Value = Value * 16 + (**p) - '0';
      UseDefault = 0;
      (*p)++;
    }

  if (UseDefault)
    return DefaultValue;
  else
    return Value;
}

#define is_cmd(str) ((strlen(str) == strlen(cmd)) && strncmp((const char *)&cmd, str, strlen(str)) == 0)
#define if_cmd(str)  if (is_cmd(str))

typedef void (t_console_cmd_handler) (char *p);

typedef struct {
    const char *name;
    t_console_cmd_handler *handler;
    const char *help;
    unsigned char is_job;
} t_cmd;

void do_help(char *p);
void do_quit(char *p);
void do_init(char *p);
void do_enable(char *p);
void do_disable(char *p);
void do_cleanup(char *p);
void do_rfcomm(char *p);
void do_rfc_con( char *p);
void do_rfc_con_for_test_msc_data(char *p);
void do_role_switch(char *p);
void do_rfc_rls (char *p);
void do_rfcomm_disc_frm_server(char *p);
void do_rfcomm_server(char *p);
void do_rfc_send_data (char *p);
/*******************************************************************
 *
 *  CONSOLE COMMAND TABLE
 *
*/

const t_cmd console_cmd_list[] =
{
    /*
     * INTERNAL
     */

    { "help", do_help, "lists all available console commands", 0 },
    { "quit", do_quit, "", 0},

    /*
     * API CONSOLE COMMANDS
     */

     /* Init and Cleanup shall be called automatically */
    { "enable", do_enable, ":: enables bluetooth", 0 },
    { "disable", do_disable, ":: disables bluetooth", 0 },
    /* add here */
    { "rfcomm", do_rfcomm, "rfcomm test", 0},
    { "server_rfcomm", do_rfcomm_server ,"rfcomm server test", 0},
    { "dis_client", do_rfcomm_disc_frm_server, "disc from Server", 0},
    { "rfc_con", do_rfc_con, "rfc_con", 0 },
    { "rfc_msccon", do_rfc_con_for_test_msc_data, "rfc_msccon", 0 },
    { "rfc_rls", do_rfc_rls, "rls",     0 },
    { "rfc_senddata", do_rfc_send_data, "rfc_senddata",     0 },
    { "role_sw", do_role_switch, "role_sw", 0},
    /* last entry */
    {NULL, NULL, "", 0},
};
static int console_cmd_maxlen = 0;

static void *cmdjob_handler(void *param)
{
    char *job_cmd = (char*)param;

    bdt_log("cmdjob starting (%s)", job_cmd);

    process_cmd(job_cmd, 1);

    bdt_log("cmdjob terminating");

    free(job_cmd);
    return NULL;
}

static int create_cmdjob(char *cmd)
{
    pthread_t thread_id;
    char *job_cmd;

    job_cmd = (char*)calloc(1, strlen(cmd)+1); /* freed in job handler */
    if (job_cmd)
    {
        strlcpy(job_cmd, cmd, sizeof(job_cmd));
        if (pthread_create(&thread_id, NULL,
                           cmdjob_handler, (void*)job_cmd)!=0)
            perror("pthread_create");
    }
    else
        bdt_log("Rfcomm_test: Cannot allocate memory for cmdjob");
    return 0;
}

/*******************************************************************************
 ** Load stack lib
 *******************************************************************************/

#define BLUETOOTH_LIBRARY_NAME "libbluetooth_qti.so"
int load_bt_lib(const bt_interface_t** interface) {
  const char* sym = BLUETOOTH_INTERFACE_STRING;
  bt_interface_t* itf = nullptr;

  // Always try to load the default Bluetooth stack on GN builds.
  const char* path = BLUETOOTH_LIBRARY_NAME;
  void* handle = dlopen(path, RTLD_NOW);
  if (!handle) {
    const char* err_str = dlerror();
    printf("failed to load Bluetooth library\n");
    goto error;
  }

  // Get the address of the bt_interface_t.
  itf = (bt_interface_t*)dlsym(handle, sym);
  if (!itf) {
    printf("failed to load symbol from Bluetooth library\n");
    goto error;
  }

  // Success.
  printf(" loaded HAL Success\n");
  *interface = itf;
  return 0;

error:
  *interface = NULL;
  if (handle) dlclose(handle);

  return -EINVAL;
}

int HAL_load(void)
{
    if (load_bt_lib((bt_interface_t const**)&sBtInterface)) {
        printf("No Bluetooth Library found\n");
        return -1;
    }
    return 0;
}

int HAL_unload(void)
{
    int err = 0;

    bdt_log("Unloading HAL lib");

    sBtInterface = NULL;
    sRfcInterface = NULL;

    bdt_log("HAL library unloaded (%s)", strerror(err));

    return err;
}

/*******************************************************************************
 ** HAL test functions & callbacks
 *******************************************************************************/

void setup_test_env(void)
{
    int i = 0;

    while (console_cmd_list[i].name != NULL)
    {
        console_cmd_maxlen = MAX(console_cmd_maxlen, (int)strlen(console_cmd_list[i].name));
        i++;
    }
}

void check_return_status(bt_status_t status)
{
    if (status != BT_STATUS_SUCCESS)
    {
        bdt_log("HAL REQUEST FAILED status : %d (%s)", status, dump_bt_status(status));
    }
    else
    {
        bdt_log("HAL REQUEST SUCCESS");
    }
}

static void adapter_state_changed(bt_state_t state)
{
    bdt_log("ADAPTER STATE UPDATED : %s", (state == BT_STATE_OFF)?"OFF":"ON");
    if (state == BT_STATE_ON) {
        bt_enabled = 1;
    } else {
        bt_enabled = 0;
    }
}

static void le_test_mode(bt_status_t status, uint16_t packet_count)
{
    bdt_log("LE TEST MODE END status:%s number_of_packets:%d", dump_bt_status(status), packet_count);
}

static void device_found_cb(int num_properties, bt_property_t *properties)
{
    int i;
    for (i = 0; i < num_properties; i++)
    {
        if (properties[i].type == BT_PROPERTY_BDNAME)
        {
            bdt_log("AP name is : %s\n",
                    (char*)properties[i].val);
        }
    }
}

static bt_callbacks_t bt_callbacks = {
    sizeof(bt_callbacks_t),
    adapter_state_changed,
    NULL,                        /*adapter_properties_cb */
    NULL,                        /* remote_device_properties_cb */
    device_found_cb,             /* device_found_cb */
    NULL,                        /* discovery_state_changed_cb */
    NULL,                        /* pin_request_cb  */
    ssp_remote_requst_callback,  /* ssp_request_cb  */
    NULL,                        /*bond_state_changed_cb */
    NULL,                        /*address_consolidate_cb*/
    NULL,                        /* acl_state_changed_cb */
    NULL,
    NULL,                        /* thread_evt_cb */
//    NULL,                      /*authorize_request_cb */
#if BLE_INCLUDED == TRUE
    le_test_mode,                /* le_test_mode_cb */
#else
    NULL,
#endif
    NULL,                       /* energy_info_cb */
    NULL,                       /* link_quality_report_cb */
    NULL,                        /* generate_local_oob_data_cb */
    NULL,                        /* switch_buffer_size_cb */
    NULL                        /* switch_codec_cb */
};

static bool set_wake_alarm(uint64_t delay_millis, bool should_wake, alarm_cb cb, void *data)
{
    static timer_t timer;
    static bool timer_created;

    if (!timer_created)
    {
        struct sigevent sigevent;
        memset(&sigevent, 0, sizeof(sigevent));
        sigevent.sigev_notify = SIGEV_THREAD;
        sigevent.sigev_notify_function = (void (*)(union sigval))cb;
        sigevent.sigev_value.sival_ptr = data;
        timer_create(CLOCK_MONOTONIC, &sigevent, &timer);
        timer_created = true;
    }

    struct itimerspec new_value;
    new_value.it_value.tv_sec = delay_millis / 1000;
    new_value.it_value.tv_nsec = (delay_millis % 1000) * 1000 * 1000;
    new_value.it_interval.tv_sec = 0;
    new_value.it_interval.tv_nsec = 0;
    timer_settime(timer, 0, &new_value, NULL);

    return true;
}

static int acquire_wake_lock(const char *lock_name)
{
    return BT_STATUS_SUCCESS;
}

static int release_wake_lock(const char *lock_name)
{
    return BT_STATUS_SUCCESS;
}

static bt_os_callouts_t callouts = {
    sizeof(bt_os_callouts_t),
    set_wake_alarm,
    acquire_wake_lock,
    release_wake_lock,
};

/* Rfcomm Client */
void bdt_rfcomm(void)
{
    bdt_log("rfcomm client");
    if (btvendorInterface)
        sRfcInterface = (btrfcomm_interface_t *) btvendorInterface->get_testapp_interface(TEST_APP_RFCOMM);
    if (sRfcInterface)
    {
       sRfcInterface->rdut_rfcomm(0);
    }
    else
    {
        bdt_log("interface not loaded");
    }
}

/* Rfcomm  Server */
void bdt_rfcomm_server (void)
{
    bdt_log("rfcomm server");
    if (btvendorInterface)
        sRfcInterface = (btrfcomm_interface_t *) btvendorInterface->get_testapp_interface(TEST_APP_RFCOMM);
    if (sRfcInterface)
    {
        sRfcInterface->rdut_rfcomm(1);
    }
    else
    {
        bdt_log("interface not loaded");
    }
}

/* Rfcomm disconnect */
void bdt_rfcomm_disc_from_server(void)
{
    bdt_log("rfcomm disc from Server");
    if (btvendorInterface)
        sRfcInterface = (btrfcomm_interface_t *) btvendorInterface->get_testapp_interface(TEST_APP_RFCOMM);
    if (sRfcInterface)
    {
        sRfcInterface->rdut_rfcomm(3);
    }
    else
    {
        bdt_log("interface not loaded");
    }
}

void bdt_init(void)
{
    bdt_log("INIT BT ");
    status = (bt_status_t)sBtInterface->init(&bt_callbacks, false, false, 0, nullptr, false, NULL);
    if (status == BT_STATUS_SUCCESS) {
        // Get Vendor Interface
        btvendorInterface = (btvendor_interface_t *)sBtInterface->get_profile_interface(BT_PROFILE_VENDOR_ID);
        if (!btvendorInterface) {
            bdt_log("Error in loading vendor interface ");
            exit(0);
        }
        status = (bt_status_t)sBtInterface->set_os_callouts(&callouts);
    }
    check_return_status(status);
}

void bdt_enable(void)
{
    bdt_log("ENABLE BT");
    if (bt_enabled) {
        bdt_log("Bluetooth is already enabled");
        return;
    }
    status = (bt_status_t)sBtInterface->enable();

    check_return_status(status);
}

void bdt_disable(void)
{
    bdt_log("DISABLE BT");
    if (!bt_enabled) {
        bdt_log("Bluetooth is already disabled");
        return;
    }
    status = (bt_status_t)sBtInterface->disable();

    check_return_status(status);
}

#define HCI_LE_RECEIVER_TEST_OPCODE 0x201D
#define HCI_LE_TRANSMITTER_TEST_OPCODE 0x201E
#define HCI_LE_END_TEST_OPCODE 0x201F

void bdt_cleanup(void)
{
    bdt_log("CLEANUP");
    sBtInterface->cleanup();
}

/*******************************************************************************
 ** Console commands
 *******************************************************************************/

static int pos = 0;

void do_help(char *p)
{
    int i = 0;
    char line[128];

    while (console_cmd_list[i].name != NULL)
    {
        pos = snprintf(line, sizeof(line), "%s", (char*)console_cmd_list[i].name);
        bdt_log("%s %s\n", (char*)line, (char*)console_cmd_list[i].help);
        i++;
    }
}

void do_quit(char *p)
{
    bdt_shutdown();
}

/*******************************************************************
 *
 *  BT TEST  CONSOLE COMMANDS
 *
 *  Parses argument lists and passes to API test function
 *
*/

void do_init(char *p)
{
    bdt_init();
}

void do_enable(char *p)
{
    bdt_enable();
}

void do_disable(char *p)
{
    bdt_disable();
}

void do_cleanup(char *p)
{
    bdt_cleanup();
}

/********* Rfcomm Test toot ***************/
void do_rfcomm(char *p)
{
    bdt_rfcomm();
}


/**********************************************
  Rfcomm connection to remote device
************************************************/
void do_rfc_con( char *p)
{
    char            buf[64];
    tRFC            conn_param;

    bdt_log("bdt do_rfc_con");
    memset(buf, 0, 64);
    /*Enter BD address */
    get_str(&p, buf);
    RawAddress::FromString(buf, conn_param.data.conn.bdadd);
    memset(buf ,0 , 64);
    get_str(&p, buf);
    conn_param.data.conn.scn = atoi(buf);
    bdt_log("SCN =%d",conn_param.data.conn.scn);
    /* Connection */
    conn_param.param = RFC_TEST_CLIENT;
    if (btvendorInterface)
        sRfcInterface = (btrfcomm_interface_t *) btvendorInterface->get_testapp_interface(TEST_APP_RFCOMM);
    if (sRfcInterface)
    {
        sRfcInterface->rdut_rfcomm_test_interface(&conn_param);
    }
    else
    {
        bdt_log("interface not loaded");
    }
}


/* For PTS test case  BV21 and BV22 */

void do_rfc_con_for_test_msc_data(char *p)
{
    char            buf[64];
    tRFC            conn_param;

    bdt_log("bdt do_rfc_con_for_test_msc_data");
    memset(buf, 0, 64);
    /*Enter BD address */
    get_str(&p, buf);
    RawAddress::FromString(buf, conn_param.data.conn.bdadd);
    memset(buf ,0 , 64);
    get_str(&p, buf);
    conn_param.data.conn.scn = atoi(buf);
    bdt_log("SCN =%d",conn_param.data.conn.scn);
    /* Connection */
    conn_param.param = RFC_TEST_CLIENT_TEST_MSC_DATA;
    if (btvendorInterface)
        sRfcInterface = (btrfcomm_interface_t *) btvendorInterface->get_testapp_interface(TEST_APP_RFCOMM);
    if (sRfcInterface)
    {
        sRfcInterface->rdut_rfcomm_test_interface(&conn_param);
    }
    else
    {
        bdt_log("interface not loaded");
    }
}



/* Role Switch */
void do_role_switch(char *p)
{
    char   buf[64];
    tRFC conn_param ;

    bdt_log("bdt do_role_switch");
    memset(buf ,0 , 64);
    /* Bluetooth Device address */
    get_str(&p, buf);
    RawAddress::FromString(buf, conn_param.data.conn.bdadd);
    memset(buf ,0 , 64);
    get_str(&p, buf);
    conn_param.data.role_switch.role = atoi(buf);
    /* Role Switch */
    conn_param.param = RFC_TEST_ROLE_SWITCH; //role switch
    if (btvendorInterface)
        sRfcInterface = (btrfcomm_interface_t *) btvendorInterface->get_testapp_interface(TEST_APP_RFCOMM);
    if (sRfcInterface)
    {
        sRfcInterface->rdut_rfcomm_test_interface(&conn_param);
    }
    else
    {
        bdt_log("interface not loaded");
    }
}

void do_rfc_rls (char *p)
{
    tRFC  conn_param;

    bdt_log("bdt rfc_rls");
    conn_param.param = RFC_TEST_FRAME_ERROR;
    if (btvendorInterface)
        sRfcInterface = (btrfcomm_interface_t *) btvendorInterface->get_testapp_interface(TEST_APP_RFCOMM);
    if (sRfcInterface)
    {
        sRfcInterface->rdut_rfcomm_test_interface(&conn_param);
    }
    else
    {
        bdt_log("interface not loaded");
    }

}

void do_rfc_send_data (char *p)
{
    tRFC  conn_param;

    bdt_log("bdt rfc_send_data");
    conn_param.param = RFC_TEST_WRITE_DATA;
    if (btvendorInterface)
        sRfcInterface = (btrfcomm_interface_t *) btvendorInterface->get_testapp_interface(TEST_APP_RFCOMM);
    if (sRfcInterface)
    {
        sRfcInterface->rdut_rfcomm_test_interface(&conn_param);
    }
    else
    {
        bdt_log("interface not loaded");
    }

}


void do_rfcomm_server(char *p)
{
    bdt_rfcomm_server();
}

void do_rfcomm_disc_frm_server(char *p)
{
   bdt_rfcomm_disc_from_server();
}

/*
 * Main console command handler
*/

static void process_cmd(char *p, unsigned char is_job)
{
    char cmd[128];
    int i = 0;
    char *p_saved = p;

    get_str(&p, cmd);

    /* table commands */
    while (console_cmd_list[i].name != NULL)
    {
        if (is_cmd(console_cmd_list[i].name))
        {
            if (!is_job && console_cmd_list[i].is_job)
                create_cmdjob(p_saved);
            else
            {
                console_cmd_list[i].handler(p);
            }
            return;
        }
        i++;
    }
    bdt_log("%s : unknown command\n", p_saved);
    do_help(NULL);
}

int main (int argc, char * argv[])
{

    config_permissions();
    bdt_log("\n:::::::::::::::::::::::::::::::::::::::::::::::::::");
    bdt_log(":: Bluedroid test app starting");

    if ( HAL_load() < 0 ) {
        perror("HAL failed to initialize, exit\n");
        unlink(PID_FILE);
        exit(0);
    }

    setup_test_env();

    /* Automatically perform the init */
    bdt_init();

    while(!main_done)
    {
        char line[128];

        /* command prompt */
        printf( ">" );
        fflush(stdout);

        fgets (line, 128, stdin);

        if (line[0]!= '\0')
        {
            /* remove linefeed */
            line[strlen(line)-1] = 0;

            process_cmd(line, 0);
            memset(line, '\0', 128);
        }
    }

    /* FIXME: Commenting this out as for some reason, the application does not exit otherwise*/
    //bdt_cleanup();
//cleanup:
    HAL_unload();

    bdt_log(":: Bluedroid test app terminating");

    return 0;
}
