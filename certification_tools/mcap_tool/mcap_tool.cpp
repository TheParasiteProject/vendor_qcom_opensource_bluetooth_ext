/******************************************************************************
 *
 *  Copyright (C) 2015, The linux Foundation. All rights reserved.
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
 *  Filename:      mcap_tool.c
 *
 *  Description:   Bluedroid MCAP TOOL application
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
#include "l2c_api.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <netdb.h>

#include <private/android_filesystem_config.h>
#include <android/log.h>

#include <hardware/hardware.h>
#include <hardware/bluetooth.h>
#include <hardware/vendor.h>
#include "bt_testapp.h"
#include "mca_defs.h"
#include "mca_api.h"

/************************************************************************************
**  Constants & Macros
************************************************************************************/
//#define   TRUE    1
//#define   FALSE   0

#define PID_FILE "/data/.bdt_pid"

#ifndef MAX
#define MAX(x, y) ((x) > (y) ? (x) : (y))
#endif

#define CASE_RETURN_STR(const) case const: return #const;
#define TRANSPORT_BREDR 1  //Add tranport parameter to create bond

/************************************************************************************
**  Local type definitions
************************************************************************************/

/************************************************************************************
**  Static variables
************************************************************************************/

static unsigned char main_done = 0;
static bt_status_t status;
static bool strict_mode = FALSE;

/* Main API */
const bt_interface_t* sBtInterface = NULL;
const btvendor_interface_t *btvendorInterface = NULL;

static gid_t groups[] = { AID_NET_BT, AID_INET, AID_NET_BT_ADMIN,
                          AID_SYSTEM, AID_MISC, AID_SDCARD_RW,
                          AID_NET_ADMIN, AID_VPN};

/* Set to 1 when the Bluedroid stack is enabled */
static unsigned char bt_enabled = 0;




enum {
    DISCONNECT,
    CONNECTING,
    CONNECTED,
    DISCONNECTING
};
static int      g_AdapterState     = BT_STATE_OFF;
static int      g_PairState     = BT_BOND_STATE_NONE;

btmcap_interface_t   *sMcapIface  = NULL;
tMCA_HANDLE    g_Mcap_Handle = 0;
tMCA_DEP       g_Mcap_Dep    = 0;
tL2CAP_FCR_OPTS    g_fcr_opts   = {
    L2CAP_FCR_ERTM_MODE,
    MCA_FCR_OPT_TX_WINDOW_SIZE,   /* Tx window size */
    MCA_FCR_OPT_MAX_TX_B4_DISCNT, /* Maximum transmissions before disconnecting */
    MCA_FCR_OPT_RETX_TOUT,        /* Retransmission timeout (2 secs) */
    MCA_FCR_OPT_MONITOR_TOUT,     /* Monitor timeout (12 secs) */
    MCA_FCR_OPT_MPS_SIZE          /* MPS segment size */
};

tMCA_CHNL_CFG g_chnl_cfg = {
    {
        L2CAP_FCR_ERTM_MODE,
        MCA_FCR_OPT_TX_WINDOW_SIZE,   /* Tx window size */
        MCA_FCR_OPT_MAX_TX_B4_DISCNT, /* Maximum transmissions before disconnecting */
        MCA_FCR_OPT_RETX_TOUT,        /* Retransmission timeout (2 secs) */
        MCA_FCR_OPT_MONITOR_TOUT,     /* Monitor timeout (12 secs) */
        MCA_FCR_OPT_MPS_SIZE          /* MPS segment size */
    },
    BT_DEFAULT_BUFFER_SIZE,
    BT_DEFAULT_BUFFER_SIZE,
    BT_DEFAULT_BUFFER_SIZE,
    BT_DEFAULT_BUFFER_SIZE,
    MCA_FCS_NONE,
    572
};

uint16_t   g_Peer_Mtu  = 0;
uint16_t   g_Mdl_Id    = 0;
tMCA_DL  g_Mdl       = 0;
tMCA_CL  g_Mcl  = 0;


/************************************************************************************
**  Static functions
************************************************************************************/

static void process_cmd(char *p, unsigned char is_job);
static void bdt_log(const char *fmt_str, ...);

/************************************************************************************
**  Externs
************************************************************************************/

/************************************************************************************
**  MCAP Callbacks
************************************************************************************/
static void mcap_ctrl_cb(tMCA_HANDLE handle, tMCA_CL mcl, uint8_t event, tMCA_CTRL *p_data)
{
    tMCA_RESULT Ret;
    //printf("%s:: handle=%d, mcl=%d, event=0x%x, g_Mdl=%d, g_Mdl_Id=%d \n", __FUNCTION__, handle, mcl, event, g_Mdl, g_Mdl_Id);
    switch(event)
    {
        case MCA_CREATE_IND_EVT:
           //printf("%s::Create_ind::Mdl_Id=%d, OpCode=%d, dep_id=%d, cfg=%d \n", __FUNCTION__, p_data->create_ind.mdl_id,
           //p_data->create_ind.op_code, p_data->create_ind.dep_id, p_data->create_ind.cfg);
           g_Mdl = p_data->create_ind.mdl_id;
           Ret = sMcapIface->CreateMdlRsp(mcl, p_data->create_ind.dep_id,
                                  g_Mdl, p_data->create_ind.cfg, MCA_SUCCESS, &g_chnl_cfg);
        break;

        case MCA_CONNECT_IND_EVT:
            //printf("%s::Connect_Ind:: peer_mtu=%d \n", __FUNCTION__, p_data->connect_ind.mtu);
            g_Mcl = mcl;
        break;

        case MCA_DISCONNECT_IND_EVT:
            g_Mcl = 0;
        break;

        case MCA_OPEN_IND_EVT:
        case MCA_OPEN_CFM_EVT:
           g_Mdl_Id = p_data->open_ind.mdl_id;
           g_Mdl    = p_data->open_ind.mdl;
           g_Peer_Mtu  = p_data->open_ind.mtu;
        break;

        case MCA_RECONNECT_IND_EVT:
            //printf("%s::Reconnect Ind:: Mdl_Id=%d, g_Mdl_Id=%d\n", __FUNCTION__, p_data->reconnect_ind.mdl_id, g_Mdl_Id);
            Ret = sMcapIface->ReconnectMdlRsp(mcl, g_Mcap_Dep, p_data->reconnect_ind.mdl_id, (g_Mdl_Id==p_data->reconnect_ind.mdl_id) ?MCA_RSP_SUCCESS :MCA_RSP_BAD_MDL, &g_chnl_cfg);
        break;

       case MCA_DELETE_IND_EVT:
           //printf("%s::Delete Ind:: Mdl_Id=%d\n", __FUNCTION__, p_data->delete_ind.mdl_id);
           if((0xffff==p_data->delete_ind.mdl_id)||(g_Mdl_Id == p_data->delete_ind.mdl_id)) g_Mdl_Id = 0;
       break;

       case MCA_SYNC_CAP_IND_EVT:
           //printf("%s::Sync Cap Ind::\n", __FUNCTION__);
       break;

       case MCA_ABORT_IND_EVT:
           //printf("%s::Abort_Ind::Mdl_Id=%d, opCode=%d \n", __FUNCTION__, p_data->abort_ind.mdl_id, p_data->abort_ind.op_code);
       break;
    }
}

static void mcap_data_cb(tMCA_DL mdl, BT_HDR *p_pkt)
{
    //printf("%s:: mdl=%d, event=%d, len=%d, offset=%d, layer_specific=%d\n", __FUNCTION__, mdl, p_pkt->event, p_pkt->len, p_pkt->offset, p_pkt->layer_specific);
}



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
               ((unsigned int)p-(unsigned int)data) );
        }

        c = *p;
        if (isalnum(c) == 0) {
            c = '.';
        }

        /* store hex str (for left side) */
        snprintf(bytestr, sizeof(bytestr), "%02X ", *p);
        strlcat(hexstr, bytestr, sizeof(hexstr)-strlen(hexstr)-1);

        /* store char str (for right side) */
        snprintf(bytestr, sizeof(bytestr), "%c", c);
        strlcat(charstr, bytestr, sizeof(charstr)-strlen(charstr)-1);

        if(n%16 == 0) {
            /* line completed */
            bdt_log("[%4.4s]   %-50.50s  %s\n", addrstr, hexstr, charstr);
            hexstr[0] = 0;
            charstr[0] = 0;
        } else if(n%8 == 0) {
            /* half line: add whitespaces */
            strlcat(hexstr, "  ", sizeof(hexstr)-strlen(hexstr)-1);
            strlcat(charstr, " ", sizeof(charstr)-strlen(charstr)-1);
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

uint32_t get_hex_any(char **p, int DefaultValue, unsigned int NumOfNibble)
{
  uint32_t Value = 0;
  unsigned char   UseDefault;
  //unsigned char   NumOfNibble = 8;    //Since we are returning uint32, max allowed is 4 bytes(8 nibbles).

  UseDefault = 1;
  skip_blanks(p);

  while ((NumOfNibble) && (((**p)<= '9' && (**p)>= '0') ||
          ((**p)<= 'f' && (**p)>= 'a') ||
          ((**p)<= 'F' && (**p)>= 'A')) )
    {
      if (**p >= 'a')
        Value = Value * 16 + (**p) - 'a' + 10;
      else if (**p >= 'A')
        Value = Value * 16 + (**p) - 'A' + 10;
      else
        Value = Value * 16 + (**p) - '0';
      UseDefault = 0;
      (*p)++;
      NumOfNibble--;
    }

  if (UseDefault)
    return DefaultValue;
  else
    return Value;
}
uint32_t get_hex(char **p, int DefaultValue)
{
    //unsigned char   NumOfNibble = 8;      //Since we are returning uint32, max allowed is 4 bytes(8 nibbles).
   return (get_hex_any(p, DefaultValue, 8));
}
uint32_t get_hex_byte(char **p, int DefaultValue)
{
   return (get_hex_any(p, DefaultValue, 2));
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


void do_init(char *p);
void do_enable(char *p);
void do_disable(char *p);
void do_cleanup(char *p);
void do_quit(char *p);
void do_help(char *p);
void do_mcap_register(char *p);
void do_mcap_deregister(char *p);
void do_mcap_create_dep(char *p);
static void do_mcap_delete_dep(char *p);
static void do_mcap_connect(char *p);
static void do_mcap_disconnect(char *p);
static void do_mcap_create_mdl(char *p);
static void do_mcap_close(char *p);
static void do_pairing(char *p);

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
    { "pair", do_pairing, ":: BdAddr<00112233445566>", 0 },
    { "register", do_mcap_register, "::Ctrl_Psm<hex>, Data_Psm<hex>, Security<0-10>", 0 },
    { "deregister", do_mcap_deregister, "::", 0 },
    { "create_data_endpoint", do_mcap_create_dep, "::Type<0-Echo, 1-NormalData>", 0 },
    { "delete_data_endpoint", do_mcap_delete_dep, "::", 0 },
    { "connect", do_mcap_connect, ":: BdAddr<00112233445566>, Ctrl_Psm<hex>, SecMask<int>", 0 },
    { "disconnect", do_mcap_disconnect, ":: BdAddr<00112233445566>", 0 },
    { "create_mdl", do_mcap_create_mdl, ":: Data_Psm<hex>", 0 },
    { "close_data_channel", do_mcap_close, "::", 0 },
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
    if(job_cmd)
    {
        strlcpy(job_cmd, cmd, strlen(job_cmd)+1);

        if (pthread_create(&thread_id, NULL, cmdjob_handler, (void *)job_cmd) != 0)
            perror("pthread_create");
    }
    else
        bdt_log("Mecap_test: Cannot Allocate memory for cmdjob");


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
    int V1 = 1000, V2=2;
    bt_property_t property = {BT_PROPERTY_ADAPTER_DISCOVERABLE_TIMEOUT, 4, &V1};
    bt_property_t property1 = {BT_PROPERTY_ADAPTER_SCAN_MODE, 2, &V2};
    bt_property_t property2 ={BT_PROPERTY_BDNAME,6, (void *)"Bluedroid"};

    g_AdapterState = state;

    if (state == BT_STATE_ON) {
        bt_enabled = 1;
        status = (bt_status_t)sBtInterface->set_adapter_property(&property1);
        status = (bt_status_t)sBtInterface->set_adapter_property(&property);
        status = (bt_status_t)sBtInterface->set_adapter_property(&property2);
    }
    else {
       bt_enabled = 0;
    }
}

static void adapter_properties_changed(bt_status_t status, int num_properties, bt_property_t *properties)
{
    char Bd_addr[15] = {0};
    if(NULL == properties)
    {
        printf("properties is null\n");
        return;
    }
    switch(properties->type)
    {
        case BT_PROPERTY_BDADDR:
        memcpy(Bd_addr, properties->val, properties->len);
        printf("Local Bd Addr = %02x:%02x:%02x:%02x:%02x:%02x\n", Bd_addr[0], Bd_addr[1], Bd_addr[2], Bd_addr[3], Bd_addr[4], Bd_addr[5]);
        break;
        default :
        break;
    }
    return;
}

static void discovery_state_changed(bt_discovery_state_t state)
{
    printf("Discovery State Updated : %s\n", (state == BT_DISCOVERY_STOPPED)?"STOPPED":"STARTED");
}


static void pin_request_cb(RawAddress *remote_bd_addr, bt_bdname_t *bd_name, uint32_t cod, bool min_16_digit)
{
    bt_pin_code_t pincode = {{ 0x31, 0x32, 0x33, 0x34}};

    if(BT_STATUS_SUCCESS != sBtInterface->pin_reply(remote_bd_addr, TRUE, 4, &pincode))
    {
        printf("Pin Reply failed\n");
    }
}

static void ssp_request_cb(RawAddress *remote_bd_addr, bt_bdname_t *bd_name,
                           uint32_t cod, bt_ssp_variant_t pairing_variant, uint32_t pass_key)
{
    printf("ssp_request_cb : %s %d %u\n", bd_name->name, pairing_variant, pass_key);
    if(BT_STATUS_SUCCESS != sBtInterface->ssp_reply(remote_bd_addr, pairing_variant, TRUE, pass_key))
    {
        printf("SSP Reply failed\n");
    }
}

static void bond_state_changed_cb(bt_status_t status, RawAddress *remote_bd_addr, bt_bond_state_t state)
{
    printf("Bond State Changed = %d\n", state);
    g_PairState = state;
}

static void acl_state_changed(bt_status_t status, RawAddress *remote_bd_addr, bt_acl_state_t state)
{
    printf("acl_state_changed : acl status=%s \n", 
    (state == BT_ACL_STATE_CONNECTED)?"ACL Connected" :"ACL Disconnected"
    );
}

static void le_test_mode(bt_status_t status, uint16_t packet_count)
{
    bdt_log("LE TEST MODE END status:%s number_of_packets:%d", dump_bt_status(status), packet_count);
}

static bt_callbacks_t bt_callbacks = {
    sizeof(bt_callbacks_t),
    adapter_state_changed,
    adapter_properties_changed, /*adapter_properties_cb */
    NULL, /* remote_device_properties_cb */
    NULL, /* device_found_cb */
    discovery_state_changed, /* discovery_state_changed_cb */
    pin_request_cb, /* pin_request_cb  */
    ssp_request_cb, /* ssp_request_cb  */
    bond_state_changed_cb, /*bond_state_changed_cb */
    acl_state_changed, /* acl_state_changed_cb */
    NULL, /* thread_evt_cb */

  //    NULL, /*authorize_request_cb */
#if BLE_INCLUDED == TRUE
    le_test_mode, /* le_test_mode_cb */
#else
    NULL,
#endif
    NULL,
};

static bool set_wake_alarm(uint64_t delay_millis, bool should_wake, alarm_cb cb, void *data) {
  static timer_t timer;
  static bool timer_created;

  if (!timer_created) {
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

static int acquire_wake_lock(const char *lock_name) {
  return BT_STATUS_SUCCESS;
}

static int release_wake_lock(const char *lock_name) {
  return BT_STATUS_SUCCESS;
}

static bt_os_callouts_t callouts = {
    sizeof(bt_os_callouts_t),
    set_wake_alarm,
    acquire_wake_lock,
    release_wake_lock,
};

void bdt_init(void)
{
    bdt_log("INIT BT ");
    status = (bt_status_t)sBtInterface->init(&bt_callbacks, false, false, 0, false);
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

void bdt_le_test_mode(char *p)
{
    int cmd;
    unsigned char buf[3];
    int arg1, arg2, arg3;

    bdt_log("BT LE TEST MODE");
    if (!bt_enabled) {
        bdt_log("Bluetooth must be enabled for le_test to work.");
        return;
    }

    memset(buf, 0, sizeof(buf));
    cmd = get_int(&p, 0);
    switch (cmd)
    {
        case 0x1: /* RX TEST */
           arg1 = get_int(&p, -1);
           if (arg1 < 0) bdt_log("%s Invalid arguments", __FUNCTION__);
           buf[0] = arg1;
           status = (bt_status_t)sBtInterface->le_test_mode(HCI_LE_RECEIVER_TEST_OPCODE, buf, 1);
           break;
        case 0x2: /* TX TEST */
            arg1 = get_int(&p, -1);
            arg2 = get_int(&p, -1);
            arg3 = get_int(&p, -1);
            if ((arg1 < 0) || (arg2 < 0) || (arg3 < 0))
                bdt_log("%s Invalid arguments", __FUNCTION__);
            buf[0] = arg1;
            buf[1] = arg2;
            buf[2] = arg3;
            status = (bt_status_t)sBtInterface->le_test_mode(HCI_LE_TRANSMITTER_TEST_OPCODE, buf, 3);
           break;
        case 0x3: /* END TEST */
            status = (bt_status_t)sBtInterface->le_test_mode(HCI_LE_END_TEST_OPCODE, buf, 0);
           break;
        default:
            bdt_log("Unsupported command");
            return;
            break;
    }
    if (status != BT_STATUS_SUCCESS)
    {
        bdt_log("%s Test 0x%x Failed with status:0x%x", __FUNCTION__, cmd, status);
    }
    return;
}

void bdt_cleanup(void)
{
    bdt_log("CLEANUP");
    sBtInterface->cleanup();
}

/*******************************************************************************
 ** Console commands
 *******************************************************************************/

void do_help(char *p)
{
    int i = 0;
    char line[128];
    int pos = 0;

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


/*******************************************************************************
 ** MCAP API commands
 *******************************************************************************/

void do_mcap_register(char *p)
{
    tMCA_REG   Mca_Reg;
    Mca_Reg.rsp_tout = 5000;   //Need to check if we have to give in msec or seconds
    Mca_Reg.ctrl_psm = get_hex(&p, -1);  // arg1
    Mca_Reg.data_psm = get_hex(&p, -1);  // arg2
    Mca_Reg.sec_mask = get_int(&p, -1);  // arg3
    g_Mcap_Handle = sMcapIface->Register(&Mca_Reg, mcap_ctrl_cb);
    printf("%s:: Ret=%d \n", __FUNCTION__, g_Mcap_Handle);
}

void do_mcap_deregister(char *p)
{
    sMcapIface->Deregister(g_Mcap_Handle);
    printf("%s:: Handle=%d \n", __FUNCTION__, g_Mcap_Handle);
}

void do_mcap_create_dep(char *p)
{
    tMCA_RESULT  Ret  = 0;
    int         type  = 0;
    tMCA_CS     Mca_cs ;
    type    = get_int(&p, -1);  // arg1

    memset ((void*)&Mca_cs ,0 ,sizeof(tMCA_CS));
    Mca_cs.type   = (0 == type) ? MCA_TDEP_ECHO :MCA_TDEP_DATA;
    Mca_cs.max_mdl  = MCA_NUM_MDLS;
    Mca_cs.p_data_cback = mcap_data_cb;

    Ret = sMcapIface->CreateDep(g_Mcap_Handle, &g_Mcap_Dep, &Mca_cs);
    printf("%s:: Ret=%d \n", __FUNCTION__, Ret);
}

static void do_mcap_delete_dep(char *p)
{
    tMCA_RESULT   Ret  = 0;
    Ret = sMcapIface->DeleteDep(g_Mcap_Handle, g_Mcap_Dep);
    printf("%s:: Ret=%d \n", __FUNCTION__, Ret);
}

static void do_mcap_connect(char *p)
{
    tMCA_RESULT  Ret = 0;
    RawAddress bd_addr;
    uint16_t      ctrl_psm = 0;
    uint16_t      sec_mask = 0;
    char   buf[64];

    get_str(&p, buf);
    RawAddress::FromString(buf, bd_addr);
    ctrl_psm  = get_hex(&p, -1);// arg2
    sec_mask  = get_int(&p, -1);// arg3
    printf("ctrl_psm=%d, secMask=%d \n", ctrl_psm, sec_mask);
    Ret = sMcapIface->ConnectReq(g_Mcap_Handle, bd_addr, ctrl_psm, sec_mask);
    printf("%s:: Ret=%d \n", __FUNCTION__, Ret);
}

static void do_mcap_disconnect(char *p)
{
    tMCA_RESULT  Ret  = 0;
    Ret = sMcapIface->DisconnectReq(g_Mcl);
    printf("%s:: Ret=%d \n", __FUNCTION__, Ret);
}

static void do_mcap_create_mdl(char *p)
{
    tMCA_RESULT Ret  = 0;
    uint16_t  data_psm = 0;
    data_psm   = get_hex(&p, -1);  // arg1
    Ret = sMcapIface->CreateMdl(g_Mcl, g_Mcap_Dep, data_psm, 1, 1, 1, &g_chnl_cfg);
    printf("%s:: Ret=%d \n", __FUNCTION__, Ret);
}

static void do_mcap_close(char *p)
{
    tMCA_RESULT  Ret  = 0;
    Ret = sMcapIface->CloseReq(g_Mdl);
    printf("%s:: Ret=%d \n", __FUNCTION__, Ret);
}

static void do_pairing(char *p)
{
    RawAddress bd_addr;
    RawAddress::FromString(p, bd_addr);

    if(BT_STATUS_SUCCESS != sBtInterface->create_bond(&bd_addr, TRANSPORT_BREDR))
    {
        printf("Failed to Initiate Pairing \n");
        return;
    }
}


/*
 * Main console command handler
*/

static void process_cmd(char *p, unsigned char is_job)
{
    char cmd[2048];
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
    sleep(5);
    bdt_enable();
    sleep(5);

    sMcapIface  = (btmcap_interface_t *)btvendorInterface->get_testapp_interface(TEST_APP_MCAP);
    //sSmpIface = sBtInterface->get_testapp_interface(TEST_APP_SMP);
    sleep(1);
    sMcapIface->Init();

    while(!main_done)
    {
        char line[2048];

        /* command prompt */
        printf( ">" );
        fflush(stdout);

        fgets (line, 2048, stdin);

        if (line[0]!= '\0')
        {
            /* remove linefeed */
            line[strlen(line)-1] = 0;

            process_cmd(line, 0);
            memset(line, '\0', 2048);
        }
    }

    /* FIXME: Commenting this out as for some reason, the application does not exit otherwise*/
    //bdt_cleanup();

    HAL_unload();

    bdt_log(":: Bluedroid test app terminating");

    return 0;
}
