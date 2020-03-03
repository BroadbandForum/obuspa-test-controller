/*
 *
 * Copyright (C) 2019, Broadband Forum
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its
 *    contributors may be used to endorse or promote products derived from
 *    this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 *
 */


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <getopt.h>
#include <limits.h>
#include <protobuf-c/protobuf-c.h>
#include <curl/curl.h>
#include <pthread.h>
#include <signal.h>
#include <syslog.h>
#include <unistd.h>

#include "usp_err_codes.h"
#include "vendor_defs.h"
#include "vendor_api.h"
#include "usp_api.h"
#include "common_defs.h"
#include "usp-msg.pb-c.h"
#include "msg_handler.h"
#include "data_model.h"
#include "dm_access.h"
#include "path_resolver.h"
#include "device.h"
#include "text_utils.h"
#include "os_utils.h"
#include "bdc_exec.h"
#include "database.h"
#include "dm_exec.h"
#include "retry_wait.h"

#define LINE_SIZE 1024 // maximum size of a line in the input file
#define PARAM_SIZE 256 // maximum size of a header of body parameter name
#define VALUE_SIZE 256 // maximum size of a header or body parameter's value
#define MAX_PATHS 10 // maximum number of paths that can be included in a Get, Add, Set, Delete, GetSupportedDM, GetInstances
#define MAX_PARAMVALUE 10 // maximum number of parameter/value pairs that can be included in a param_setting
#define WAIT_BETWEEN_MSGS 2 // time to wait between sending messages; also wait twice this before closing connection at end

//------------------------------------------------------------------------------
// Forward declarations. Note these are not static, because we need them in the symbol table for USP_LOG_Callstack() to show them
Usp__Msg *CreateGet(char p_paths[MAX_PATHS][VALUE_SIZE], int n);
Usp__Msg *CreateGetInstances(char obj_paths[MAX_PATHS][VALUE_SIZE], int n, bool first_lvl_only);
Usp__Msg *CreateGetSupportedDM(char obj_paths[MAX_PATHS][VALUE_SIZE], int n, bool first_lvl_only, bool return_commands, bool return_events, bool return_params);
Usp__Msg *CreateGetSupportedProtocol(char version[VALUE_SIZE]);
Usp__Msg *CreateDelete(char obj_paths[MAX_PATHS][VALUE_SIZE], int n);
Usp__Msg *CreateAdd(bool allow_partial, char obj_paths[MAX_PATHS][VALUE_SIZE], bool required[MAX_PATHS][MAX_PARAMVALUE], int n_param_settings[MAX_PATHS], char param_settings_param[MAX_PATHS][MAX_PARAMVALUE][PARAM_SIZE], char param_settings_value[MAX_PATHS][MAX_PARAMVALUE][VALUE_SIZE], int n);
Usp__Msg *CreateSet(bool allow_partial, char obj_paths[MAX_PATHS][VALUE_SIZE], bool req[MAX_PATHS][MAX_PARAMVALUE], int n_p_settings[MAX_PATHS], char p_settings_param[MAX_PATHS][MAX_PARAMVALUE][PARAM_SIZE], char p_settings_value[MAX_PATHS][MAX_PARAMVALUE][VALUE_SIZE], int n);
Usp__Msg *CreateOperate(char command[VALUE_SIZE], char command_key[VALUE_SIZE], char keys[MAX_PARAMVALUE][PARAM_SIZE], char values[MAX_PARAMVALUE][VALUE_SIZE], bool send_resp, int n);
int Controller_Start(char *db_file, bool enable_mem_info);
int StartBasicAgentProcesses(void);
mtp_reply_to_t *InitializeMTPStructure(void);

// parameters collected from first line and used globally
char *msg_id = "1";
char *agent_endpoint;
char *stomp_dest = "";
char *protocol = "";
int stomp_instance = 0;
char *coap_host = "";
int coap_port = 0;
char *coap_resource = "/";
char *line_msg_type;
mtp_reply_to_t mtp_send;

/*************************************************************************
**
** ParseFirstLine
**
** Parses the first input line and sets global variable parameters
**
** \param  first input line (from controller_file); contains settings for sending messages
** \return int error
**
**************************************************************************/
int ParseFirstLine(char line[LINE_SIZE])
{
    int c, x, j = 0; // j is character number in param or value
    int quote_start = 0; // set to 1 at first double quote and reset at 2nd
    int escape = 0; // set to 1 if "\" is input so next character is escaped
    char param[PARAM_SIZE] = ""; // stores parameter name
    int param_start = 1; // set to 1 if subsequent characters are to be read into param
    char value[VALUE_SIZE] = ""; // stores parameter value
    int value_start = 0; // set to 1 if subsequent characters are to be read into value

    for(x=0;x<LINE_SIZE;x++)
    {
        c = line[x]; // step through the line, character-by-character
        if(c == '\0' || c == '\n') // if end of line or end of string reached, break loop
        {
            value[j] = '\0';
            if(strcmp(param, "stomp_instance") == 0) 
                stomp_instance = atoi(value);
            else if(strcmp(param, "msg_id") == 0)
                msg_id = strdup(value);
            else if(strcmp(param, "stomp_agent_dest") == 0)
                stomp_dest = strdup(value);
            else if(strcmp(param, "to_id") == 0)
                agent_endpoint = strdup(value);
            else if(strcmp(param, "coap_host") == 0)
                coap_host = strdup(value);
	    else if(strcmp(param, "coap_port") == 0) 
                coap_port = atoi(value);
            else if(strcmp(param, "coap_resource") == 0)
                coap_resource = strdup(value);
            else
            {
            printf("Unrecognized parameter name: %s", param);
            }
            break;
        }
        else if(c == '\\' && escape == 0 && quote_start == 1) // if escape character, set escape to 1
            escape = 1;
        else if(c == '\"' && quote_start == 0 && escape == 0) // if first unescaped quote, put subsequent characters into the param or value until second unescaped quote
            quote_start = 1;
        else if(c == '\"' && quote_start == 1 && escape == 0) // if second unescaped quote, then quote is closed
            quote_start = 0;
        else if(c == ' ' && quote_start == 0) // if space not inside quotes, this delimits start of next param; if value is non-null print stuff before resetting parameters
        {
            value[j] = '\0';
            if(strcmp(param, "stomp_instance") == 0) 
                stomp_instance = atoi(value);
            else if(strcmp(param, "msg_id") == 0)
                msg_id = strdup(value);
            else if(strcmp(param, "stomp_agent_dest") == 0)
                stomp_dest = strdup(value);
            else if(strcmp(param, "to_id") == 0)
                agent_endpoint = strdup(value);
            else if(strcmp(param, "coap_host") == 0)
                coap_host = strdup(value);
	    else if(strcmp(param, "coap_port") == 0) 
                coap_port = atoi(value);
            else if(strcmp(param, "coap_resource") == 0)
                coap_resource = strdup(value);
            else
            {
            printf("Unrecognized parameter name: %s", param);
            }
            memset(param,0,PARAM_SIZE);
            memset(value,0,VALUE_SIZE);
            param_start = 1;
            value_start = 0;
            j = 0;
        }
        else if(c == ':' && quote_start == 0) // : delimits between param and value
        {
            param[j] = '\0';
            value_start = 1;
            param_start = 0;
            j = 0;
        }
        else if(param_start == 1) // everything else gets put into param or value depending on param_start and value_start
        {
            param[j] = c;
            j++;
        }
        else if(value_start == 1)
        {
            value[j] = c;
            escape = 0;
            j++;
        }
    }
    return(0);
}

/*************************************************************************
**
** ReadMessageType
**
** Parses an input line and writes to the global msg_type variable
**
** \param   input line (from controller_file) that has a USP message to send
** \return int error
**
**************************************************************************/
int ReadMessageType(char line[LINE_SIZE])
{
    int c, x, j = 0; // j is character number in param or value
    int quote_start = 0; // set to 1 at first double quote and reset at 2nd
    char param[PARAM_SIZE]; // stores parameter name
    int param_start = 1; // set to 1 if subsequent characters are to be read into param
    char value[VALUE_SIZE]; // stores parameter value
    int value_start = 0; // set to 1 if subsequent characters are to be read into value

    for(x=0;x<LINE_SIZE;x++)
    {
        c = line[x]; // step through the line, character-by-character
        if(c == '\0' || c == '\n') // if end of line or end of string reached, break loop
            break;
        else if(c == '\"' && quote_start == 0) // if first unescaped quote, put subsequent characters into the param or value until second unescaped quote
            quote_start = 1;
        else if(c == '\"' && quote_start == 1) // if second unescaped quote, then quote is closed
            quote_start = 0;
        else if(c == ' ' && quote_start == 0) // if space not inside quotes, this delimits end of name-value pair; process and break
        {
            value[j] = '\0';
            if(strcmp(param, "msg_type") == 0)
                line_msg_type = strdup(value);
            else
            {
                printf("First parameter must be msg_type and not %s.\n", param);
            }
            return(0);
        }
        else if(c == ':' && quote_start == 0) // : delimits between param and value
        {
            param[j] = '\0';
            value_start = 1;
            param_start = 0;
            j = 0;
        }
        else if(param_start == 1) // everything else gets put into param or value depending on param_start and value_start
        {
            param[j] = c;
            j++;
        }
        else if(value_start == 1)
        {
            value[j] = c;
            j++;
        }
    }
    return(0);
}

/*************************************************************************
**
** ParseGet
**
** Parses an input line with msg_type Get
**
** \param   input line (from controller_file) to parse and turn into a USP message
** \return  void
**
**************************************************************************/
int ParseGet(char line[LINE_SIZE])
{
    char p_paths[MAX_PATHS][VALUE_SIZE];
    int c, x, j = 0, n=0; // j is character number in param or value, n is number of param_paths
    int quote_start = 0; // set to 1 at first double quote and reset at 2nd
    char param[PARAM_SIZE]; // stores parameter name
    int param_start = 1; // set to 1 if subsequent characters are to be read into param
    char value[VALUE_SIZE]; // stores parameter value
    int value_start = 0; // set to 1 if subsequent characters are to be read into value
    Usp__Msg *get_message;
    mtp_reply_to_t *mtp_send_to;

    for(x=0;x<LINE_SIZE;x++)
    {
        c = line[x]; // step through the line, character-by-character
        if(c == '\0' || c == '\n') // if end of line or end of string reached, process everything and break loop
        {
            value[j] = '\0';
            if(strcmp(param, "param_paths") == 0)
            {
                strcpy(p_paths[n], value);
                n++;
            }
            break;
        }
        else if(c == '\"' && quote_start == 0) // if first unescaped quote, put subsequent characters into the param or value until second unescaped quote
            quote_start = 1;
        else if(c == '\"' && quote_start == 1) // if second unescaped quote, then quote is closed
            quote_start = 0;
        else if(c == ' ' && quote_start == 0) // if space not inside quotes, this delimits end of name-value pair; process and break
        {
            value[j] = '\0';
            if(strcmp(param, "param_paths") == 0)
            {
                strcpy(p_paths[n], value);
                n++;
            }
            memset(param,0,PARAM_SIZE);
            memset(value,0,VALUE_SIZE);
            param_start = 1;
            value_start = 0;
            j = 0;
        }
        else if(c == ':' && quote_start == 0) // : delimits between param and value
        {
            param[j] = '\0';
            value_start = 1;
            param_start = 0;
            j = 0;
        }
        else if(param_start == 1) // everything else gets put into param or value depending on param_start and value_start
        {
            param[j] = c;
            j++;
        }
        else if(value_start == 1)
        {
            value[j] = c;
            j++;
        }
    }
    // put the message in the protobufs structure and send it
    mtp_send_to = InitializeMTPStructure();
    get_message = CreateGet(p_paths, n);
    MSG_HANDLER_QueueMessage(agent_endpoint, get_message, mtp_send_to);
    usp__msg__free_unpacked(get_message, pbuf_allocator);
    return(0);
}

/*************************************************************************
**
** ParseGetSupportedDM
**
** Parses an input line with msg_type GetSupportedDM
**
** \param   input line (from controller_file) to parse and turn into a USP message
** \return  void
**
**************************************************************************/
int ParseGetSupportedDM(char line[LINE_SIZE])
{
    char obj_paths[MAX_PATHS][VALUE_SIZE];
    bool first_level_only = false;
    bool return_commands = false;
    bool return_events = false;
    bool return_params = false;
    int c, x, j = 0, n = 0; // j is character number in param or value, n is number of param_pathsint return_commands
    int quote_start = 0; // set to 1 at first double quote and reset at 2nd
    int escape = 0;
    char param[PARAM_SIZE]; // stores parameter name
    int param_start = 1; // set to 1 if subsequent characters are to be read into param
    char value[VALUE_SIZE]; // stores parameter value
    int value_start = 0; // set to 1 if subsequent characters are to be read into value
    Usp__Msg *get_supported_dm_message;
    mtp_reply_to_t *mtp_send_to;

    for(x=0;x<LINE_SIZE;x++)
    {
        c = line[x]; // step through the line, character-by-character
        if(c == '\0' || c == '\n') // if end of line or end of string reached, process everything and break loop
        {
            value[j] = '\0';
            if(strcmp(param, "obj_paths") == 0) {
                strcpy(obj_paths[n], value);
                n++; }
            else if(strcmp(param, "first_level_only") == 0) {
                if(strcmp(value, "true") == 0)
                    first_level_only = true;}
            else if(strcmp(param, "return_commands") == 0) {
                if(strcmp(value, "true") == 0)
                    return_commands = true;}
            else if(strcmp(param, "return_events") == 0) {
                if(strcmp(value, "true") == 0)
                    return_events = true;}
            else if(strcmp(param, "return_params") == 0) {
                if(strcmp(value, "true") == 0)
                    return_params = true;}
            break;
        }
        else if(c == '\\' && escape == 0 && quote_start == 1) // if escape character, set escape to 1
            escape = 1;
        else if(c == '\"' && quote_start == 0 && escape == 0) // if first quote, put subsequent characters into the param or value until second unescaped quote
            quote_start = 1;
        else if(c == '\"' && quote_start == 1 && escape == 0) // if second quote, then quote is closed
            quote_start = 0;
        else if(c == ' ' && quote_start == 0) // if space not inside quotes, this delimits end of name-value pair; process and break
        {
            value[j] = '\0';
            if(strcmp(param, "obj_paths") == 0)
            {
                strcpy(obj_paths[n], value);
                n++;
            }
            else if(strcmp(param, "first_level_only") == 0) {
                if(strcmp(value, "true") == 0)
                    first_level_only = true;}
            else if(strcmp(param, "return_commands") == 0) {
                if(strcmp(value, "true") == 0)
                    return_commands = true;}
            else if(strcmp(param, "return_events") == 0) {
                if(strcmp(value, "true") == 0)
                    return_events = true;}
            else if(strcmp(param, "return_params") == 0) {
                if(strcmp(value, "true") == 0)
                    return_params = true;}
            memset(param,0,PARAM_SIZE);
            memset(value,0,VALUE_SIZE);
            param_start = 1;
            value_start = 0;
            j = 0;
        }
        else if(c == ':' && quote_start == 0) // : delimits between param and value
        {
            param[j] = '\0';
            value_start = 1;
            param_start = 0;
            j = 0;
        }
        else if(param_start == 1) // everything else gets put into param or value depending on param_start and value_start
        {
            param[j] = c;
            j++;
        }
        else if(value_start == 1)
        {
            value[j] = c;
            escape = 0;
            j++;
        }
    }
    // put the message in the protobufs structure and send it
    mtp_send_to = InitializeMTPStructure();
    get_supported_dm_message = CreateGetSupportedDM(obj_paths, n, first_level_only, return_commands, return_events, return_params);
    MSG_HANDLER_QueueMessage(agent_endpoint, get_supported_dm_message, mtp_send_to);
    usp__msg__free_unpacked(get_supported_dm_message, pbuf_allocator);
    return(0);
}

/*************************************************************************
**
** ParseAdd
**
** Parses an input line with msg_type Add
**
** \param   input line (from controller_file) to parse and turn into a USP message
** \return  void
**
**************************************************************************/
int ParseAdd(char line[LINE_SIZE])
{
    bool allow_partial = false;
    char obj_path[MAX_PATHS][VALUE_SIZE];
    bool required[MAX_PATHS][MAX_PARAMVALUE];
    char param_settings_param[MAX_PATHS][MAX_PARAMVALUE][PARAM_SIZE];
    char param_settings_value[MAX_PATHS][MAX_PARAMVALUE][VALUE_SIZE];
    int n_param_settings[MAX_PATHS];
    int c, x, j = 0, o=0, n = 0; // j is character number in param or value, n is number of param_paths for each o obj_path
    int quote_start = 0; // set to 1 at first double quote and reset at 2nd
    int escape = 0;
    char param[PARAM_SIZE]; // stores parameter name
    int param_start = 1; // set to 1 if subsequent characters are to be read into param
    char value[VALUE_SIZE]; // stores parameter value
    int value_start = 0; // set to 1 if subsequent characters are to be read into value
    Usp__Msg *add_message;
    mtp_reply_to_t *mtp_send_to;

    for(x=0;x<LINE_SIZE;x++)
    {
        c = line[x]; // step through the line, character-by-character
        if(c == '\0' || c == '\n') // if end of line or end of string reached, break loop
            break;
        else if(c == '\\' && escape == 0 && quote_start == 1) // if escape character, set escape to 1
            escape = 1;
        else if(c == '\"' && quote_start == 0 && escape == 0) // if first quote, put subsequent characters into the param or value until second unescaped quote
            quote_start = 1;
        else if(c == '\"' && quote_start == 1 && escape == 0) // if second quote, then quote is closed
            quote_start = 0;
        else if(c == ' ' && quote_start == 0) // if space not inside quotes, this delimits end of name-value pair; process and break
        {
            value[j] = '\0';
            if((strcmp(param, "param") == 0) && (o>0))
                strcpy(param_settings_param[o-1][n], value);
            else if((strcmp(param, "value") == 0) && (o>0))
                strcpy(param_settings_value[o-1][n], value);
            else if((strcmp(param, "required") == 0) && (o>0)) {
                if(strcmp(value, "true") == 0)
                    required[o-1][n] = true;}
            else if(strcmp(param, "allow_partial") == 0) {
                if(strcmp(value, "true") == 0)
                    allow_partial = true;}
            else if((strcmp(param, "obj_path") == 0) && (o>0))
                strcpy(obj_path[o-1], value);

            memset(param,0,PARAM_SIZE);
            memset(value,0,VALUE_SIZE);
            param_start = 1;
            value_start = 0;
            j = 0;
        }
        else if(c == ':' && quote_start == 0) // : delimits between param and value
        {
            param[j] = '\0';
            value_start = 1;
            param_start = 0;
            j = 0;
        }
	else if(c == '{' && quote_start == 0)
        {
            param[j] = '\0';
            if(strcmp(param, "create_objs") == 0) {
                o++;
		n = 0; }

            memset(param,0,PARAM_SIZE);
            memset(value,0,VALUE_SIZE);
            param_start = 1;
            value_start = 0;
            j=0;
        }
        else if(c == '}' && quote_start == 0)
        {
            if(value[0] != '\0')
            {
                value[j] = '\0';
                if((strcmp(param, "param") == 0) && (o>0))
                    strcpy(param_settings_param[o-1][n], value);
		else if((strcmp(param, "value") == 0) && (o>0))
                    strcpy(param_settings_value[o-1][n], value);
                else if((strcmp(param, "required") == 0) && (o>0)){
                    if(strcmp(value, "true") == 0)
                        required[o-1][n] = true; }
                else if(strcmp(param, "allow_partial") == 0) {
                    if(strcmp(value, "true") == 0)
                        allow_partial = true; }
                else if((strcmp(param, "obj_path") == 0) && (o>0))
                    strcpy(obj_path[o-1], value);
                n++;
            }
	    else if(o>0)
	    {
                n_param_settings[o-1] = n;
		n = 0;
	    }
            memset(param,0,PARAM_SIZE);
            memset(value,0,VALUE_SIZE);
            param_start = 1;
            value_start = 0;
            j = 0;
        }
        else if(param_start == 1) // everything else gets put into param or value depending on param_start and value_start
        {
            param[j] = c;
            j++;
        }
        else if(value_start == 1)
        {
            value[j] = c;
            escape = 0;
            j++;
        }
    }
    // put the message in the protobufs structure and send it
    mtp_send_to = InitializeMTPStructure();
    add_message = CreateAdd(allow_partial, obj_path, required, n_param_settings, param_settings_param, param_settings_value, o);
    MSG_HANDLER_QueueMessage(agent_endpoint, add_message, mtp_send_to);
    usp__msg__free_unpacked(add_message, pbuf_allocator);

    return(0);
}

/*************************************************************************
**
** ParseSet
**
** Parses an input line with msg_type Set
**
** \param   input line (from controller_file) to parse and turn into a USP message
** \return  void
**
**************************************************************************/
int ParseSet(char line[LINE_SIZE])
{
    bool allow_partial = false;
    char obj_path[MAX_PATHS][VALUE_SIZE];
    bool required[MAX_PATHS][MAX_PARAMVALUE];
    char param_settings_param[MAX_PATHS][MAX_PARAMVALUE][PARAM_SIZE];
    char param_settings_value[MAX_PATHS][MAX_PARAMVALUE][VALUE_SIZE];
    int n_param_settings[MAX_PATHS];
    int c, x, j = 0, o=0, n = 0; // j is character number in param or value, n is number of param_paths for each o obj_path
    int quote_start = 0; // set to 1 at first double quote and reset at 2nd
    int escape = 0;
    char param[PARAM_SIZE]; // stores parameter name
    int param_start = 1; // set to 1 if subsequent characters are to be read into param
    char value[VALUE_SIZE]; // stores parameter value
    int value_start = 0; // set to 1 if subsequent characters are to be read into value
    Usp__Msg *set_message;
    mtp_reply_to_t *mtp_send_to;

    for(x=0;x<LINE_SIZE;x++)
    {
        c = line[x]; // step through the line, character-by-character
        if(c == '\0' || c == '\n') // if end of line or end of string reached, break loop
            break;
        else if(c == '\\' && escape == 0 && quote_start == 1) // if escape character, set escape to 1
            escape = 1;
        else if(c == '\"' && quote_start == 0 && escape == 0) // if first quote, put subsequent characters into the param or value until second unescaped quote
            quote_start = 1;
        else if(c == '\"' && quote_start == 1 && escape == 0) // if second quote, then quote is closed
            quote_start = 0;
        else if(c == ' ' && quote_start == 0) // if space not inside quotes, this delimits end of name-value pair; process and break
        {
            value[j] = '\0';
            if((strcmp(param, "param") == 0) && (o>0))
                strcpy(param_settings_param[o-1][n], value);
            else if((strcmp(param, "value") == 0) && (o>0))
                strcpy(param_settings_value[o-1][n], value);
            else if((strcmp(param, "required") == 0) && (o>0)) {
                if(strcmp(value, "true") == 0)
                    required[o-1][n] = true;}
            else if(strcmp(param, "allow_partial") == 0) {
                if(strcmp(value, "true") == 0)
                    allow_partial = true;}
            else if((strcmp(param, "obj_path") == 0) && (o>0))
                strcpy(obj_path[o-1], value);

            memset(param,0,PARAM_SIZE);
            memset(value,0,VALUE_SIZE);
            param_start = 1;
            value_start = 0;
            j = 0;
        }
        else if(c == ':' && quote_start == 0) // : delimits between param and value
        {
            param[j] = '\0';
            value_start = 1;
            param_start = 0;
            j = 0;
        }
	else if(c == '{' && quote_start == 0)
        {
            param[j] = '\0';
            if(strcmp(param, "update_objs") == 0) {
                o++;
		n = 0; }

            memset(param,0,PARAM_SIZE);
            memset(value,0,VALUE_SIZE);
            param_start = 1;
            value_start = 0;
            j=0;
        }
        else if(c == '}' && quote_start == 0)
        {
            if(value[0] != '\0')
            {
                value[j] = '\0';
                if((strcmp(param, "param") == 0) && (o>0))
                    strcpy(param_settings_param[o-1][n], value);
		else if((strcmp(param, "value") == 0) && (o>0))
                    strcpy(param_settings_value[o-1][n], value);
                else if((strcmp(param, "required") == 0) && (o>0)){
                    if(strcmp(value, "true") == 0)
                        required[o-1][n] = true; }
                else if(strcmp(param, "allow_partial") == 0) {
                    if(strcmp(value, "true") == 0)
                        allow_partial = true; }
                else if((strcmp(param, "obj_path") == 0) && (o>0))
                    strcpy(obj_path[o-1], value);
		n++;
            }
	    else if(o>0)
	    {
                n_param_settings[o-1] = n;
		n = 0;
	    }
            memset(param,0,PARAM_SIZE);
            memset(value,0,VALUE_SIZE);
            param_start = 1;
            value_start = 0;
            j = 0;
        }
        else if(param_start == 1) // everything else gets put into param or value depending on param_start and value_start
        {
            param[j] = c;
            j++;
        }
        else if(value_start == 1)
        {
            value[j] = c;
            escape = 0;
            j++;
        }
    }
    // put the message in the protobufs structure and send it
    mtp_send_to = InitializeMTPStructure();
    set_message = CreateSet(allow_partial, obj_path, required, n_param_settings, param_settings_param, param_settings_value, o);
    MSG_HANDLER_QueueMessage(agent_endpoint, set_message, mtp_send_to);
    usp__msg__free_unpacked(set_message, pbuf_allocator);
    return(0);
}

/*************************************************************************
**
** ParseDelete
**
** Parses an input line with msg_type Delete
**
** \param   input line (from controller_file) to parse and turn into a USP message
** \return  void
**
**************************************************************************/
int ParseDelete(char line[LINE_SIZE])
{
    char obj_paths[MAX_PATHS][VALUE_SIZE];
    int c, x, j = 0, n = 0; // j is character number in param or value, n is number of param_pathsint return_commands
    int quote_start = 0; // set to 1 at first double quote and reset at 2nd
    int escape = 0;
    char param[PARAM_SIZE]; // stores parameter name
    int param_start = 1; // set to 1 if subsequent characters are to be read into param
    char value[VALUE_SIZE]; // stores parameter value
    int value_start = 0; // set to 1 if subsequent characters are to be read into value
    Usp__Msg *delete_message;
    mtp_reply_to_t *mtp_send_to;

    for(x=0;x<LINE_SIZE;x++)
    {
        c = line[x]; // step through the line, character-by-character
        if(c == '\0' || c == '\n') // if end of line or end of string reached, process everything and break loop
        {
            value[j] = '\0';
            if(strcmp(param, "obj_paths") == 0) {
                strcpy(obj_paths[n], value);
                n++; }
            break;
        }
        else if(c == '\\' && escape == 0 && quote_start == 1) // if "\" escape inside quoted value, set escape to 1
            escape = 1;
        else if(c == '\"' && quote_start == 0 && escape == 0) // if first unescaped quote, put subsequent characters into the param or value until second unescaped quote
            quote_start = 1;
        else if(c == '\"' && quote_start == 1 && escape == 0) // if second unescaped quote, then quote is closed
            quote_start = 0;
        else if(c == ' ' && quote_start == 0) // if space not inside quotes, this delimits end of name-value pair; process and break
        {
            value[j] = '\0';
            if(strcmp(param, "obj_paths") == 0)
            {
                strcpy(obj_paths[n], value);
                n++;
            }
            memset(param,0,PARAM_SIZE);
            memset(value,0,VALUE_SIZE);
            param_start = 1;
            value_start = 0;
            j = 0;
        }
        else if(c == ':' && quote_start == 0) // : delimits between param and value
        {
            param[j] = '\0';
            value_start = 1;
            param_start = 0;
            j = 0;
        }
        else if(param_start == 1) // everything else gets put into param or value depending on param_start and value_start
        {
            param[j] = c;
            j++;
        }
        else if(value_start == 1)
        {
            value[j] = c;
            escape = 0;
            j++;
        }
    }
    // put the message in the protobufs structure and send it
    mtp_send_to = InitializeMTPStructure();
    delete_message = CreateDelete(obj_paths, n);
    MSG_HANDLER_QueueMessage(agent_endpoint, delete_message, mtp_send_to);
    usp__msg__free_unpacked(delete_message, pbuf_allocator);
    return(0);
}

/*************************************************************************
**
** ParseOperate
**
** Parses an input line with msg_type Operate
**
** \param   input line (from controller_file) to parse and turn into a USP message
** \return  void
**
**************************************************************************/
int ParseOperate(char line[LINE_SIZE])
{
    char command[VALUE_SIZE];
    char command_key[VALUE_SIZE];
    char param_settings_param[MAX_PARAMVALUE][PARAM_SIZE];
    char param_settings_value[MAX_PARAMVALUE][VALUE_SIZE];
    bool send_resp = false;
    int c, x, j = 0, n = 0; // j is character number in param or value, n is number of param_paths
    int quote_start = 0; // set to 1 at first double quote and reset at 2nd
    int escape = 0;
    char param[PARAM_SIZE]; // stores parameter name
    int param_start = 1; // set to 1 if subsequent characters are to be read into param
    char value[VALUE_SIZE]; // stores parameter value
    int value_start = 0; // set to 1 if subsequent characters are to be read into value
    Usp__Msg *operate_message;
    mtp_reply_to_t *mtp_send_to;

    for(x=0;x<LINE_SIZE;x++)
    {
        c = line[x]; // step through the line, character-by-character
        if(c == '\0' || c == '\n') // if end of line or end of string reached, process everything and break loop
        {
            value[j] = '\0';
            if(strcmp(param, "param") == 0) {
                strcpy(param_settings_param[n], value); }
	    else if(strcmp(param, "value") == 0) {
                strcpy(param_settings_value[n], value);
                n++; }
            else if(strcmp(param, "send_resp") == 0) {
                if(strcmp(value, "true") == 0)
                    send_resp = true;}
            else if(strcmp(param, "command") == 0)
                strcpy(command, value);
            else if(strcmp(param, "command_key") == 0)
                strcpy(command_key, value);
            break;
        }
        else if(c == '\\' && escape == 0 && quote_start == 1) // if escape character, set escape to 1
            escape = 1;
        else if(c == '\"' && quote_start == 0 && escape == 0) // if first quote, put subsequent characters into the param or value until second unescaped quote
            quote_start = 1;
        else if(c == '\"' && quote_start == 1 && escape == 0) // if second quote, then quote is closed
            quote_start = 0;
        else if(c == ' ' && quote_start == 0) // if space not inside quotes, this delimits end of name-value pair; process and break
        {
            value[j] = '\0';
            if(strcmp(param, "param") == 0) {
                strcpy(param_settings_param[n], value); }
            else if(strcmp(param, "value") == 0) {
                strcpy(param_settings_value[n], value);
                n++; }
            else if(strcmp(param, "send_resp") == 0) {
                if(strcmp(value, "true") == 0)
                    send_resp = true;}
            else if(strcmp(param, "command") == 0)
                strcpy(command, value);
            else if(strcmp(param, "command_key") == 0)
                strcpy(command_key, value);

            memset(param,0,PARAM_SIZE);
            memset(value,0,VALUE_SIZE);
            param_start = 1;
            value_start = 0;
            j = 0;
        }
        else if(c == ':' && quote_start == 0) // : delimits between param and value
        {
            param[j] = '\0';
            value_start = 1;
            param_start = 0;
            j = 0;
        }
        else if(param_start == 1) // everything else gets put into param or value depending on param_start and value_start
        {
            param[j] = c;
            j++;
        }
        else if(value_start == 1)
        {
            value[j] = c;
            escape = 0;
            j++;
        }
    }
    // put the message in the protobufs structure and send it
    mtp_send_to = InitializeMTPStructure();
    operate_message = CreateOperate(command, command_key, param_settings_param, param_settings_value, send_resp, n);
    MSG_HANDLER_QueueMessage(agent_endpoint, operate_message, mtp_send_to);
    usp__msg__free_unpacked(operate_message, pbuf_allocator);

    return(0);
}

/*************************************************************************
**
** ParseGetSupportedProtocol
**
** Parses an input line with msg_type GetSupportedProtocol
**
** \param   input line (from controller_file) to parse and turn into a USP message
** \return  void
**
**************************************************************************/
int ParseGetSupportedProtocol(char line[LINE_SIZE])
{
    char versions[MAX_PARAMVALUE];
    int c, x, j=0; // j is character number in param or value, n is number of param_paths
    int quote_start = 0; // set to 1 at first double quote and reset at 2nd
    char param[PARAM_SIZE]; // stores parameter name
    int param_start = 1; // set to 1 if subsequent characters are to be read into param
    char value[VALUE_SIZE]; // stores parameter value
    int value_start = 0; // set to 1 if subsequent characters are to be read into value
    Usp__Msg *get_supported_protocol_message;
    mtp_reply_to_t *mtp_send_to;

    for(x=0;x<LINE_SIZE;x++)
    {
        c = line[x]; // step through the line, character-by-character
        if(c == '\0' || c == '\n') // if end of line or end of string reached, process everything and break loop
        {
            value[j] = '\0';
            if(strcmp(param, "controller_supported_protocol_versions") == 0)
                strcpy(versions, value);
            break;
        }
        else if(c == '\"' && quote_start == 0) // if first quote, put subsequent characters into the param or value until second unescaped quote
            quote_start = 1;
        else if(c == '\"' && quote_start == 1) // if second quote, then quote is closed
            quote_start = 0;
        else if(c == ' ' && quote_start == 0) // if space not inside quotes, this delimits end of name-value pair; process and break
        {
            value[j] = '\0';
            if(strcmp(param, "controller_supported_protocol_versions") == 0)
                strcpy(versions, value);
            memset(param,0,PARAM_SIZE);
            memset(value,0,VALUE_SIZE);
            param_start = 1;
            value_start = 0;
            j = 0;
        }
        else if(c == ':' && quote_start == 0) // : delimits between param and value
        {
            param[j] = '\0';
            value_start = 1;
            param_start = 0;
            j = 0;
        }
        else if(param_start == 1) // everything else gets put into param or value depending on param_start and value_start
        {
            param[j] = c;
            j++;
        }
        else if(value_start == 1)
        {
            value[j] = c;
            j++;
        }
    }
    // put the message in the protobufs structure and send it
    mtp_send_to = InitializeMTPStructure();
    get_supported_protocol_message = CreateGetSupportedProtocol(versions);
    MSG_HANDLER_QueueMessage(agent_endpoint, get_supported_protocol_message, mtp_send_to);
    usp__msg__free_unpacked(get_supported_protocol_message, pbuf_allocator);
    return(0);
}

/*************************************************************************
**
** ParseGetInstances
**
** Parses an input line with msg_type GetInstances
**
** \param   input line (from controller_file) to parse and turn into a USP message
** \return  void
**
**************************************************************************/
int ParseGetInstances(char line[LINE_SIZE])
{
    char obj_paths[MAX_PATHS][VALUE_SIZE];
    bool first_level_only = false;
    int c, x, j = 0, n = 0; // j is character number in param or value, n is number of param_pathsint return_commands
    int quote_start = 0; // set to 1 at first double quote and reset at 2nd
    int escape = 0; // if inside quotes, set to 1 if character is "\"; reset after next character
    char param[PARAM_SIZE]; // stores parameter name
    int param_start = 1; // set to 1 if subsequent characters are to be read into param
    char value[VALUE_SIZE]; // stores parameter value
    int value_start = 0; // set to 1 if subsequent characters are to be read into value
    Usp__Msg *get_instances_message;
    mtp_reply_to_t *mtp_send_to;

    for(x=0;x<LINE_SIZE;x++)
    {
        c = line[x]; // step through the line, character-by-character
        if(c == '\0' || c == '\n') // if end of line or end of string reached, process everything and break loop
        {
            value[j] = '\0';
            if(strcmp(param, "obj_paths") == 0) {
                strcpy(obj_paths[n], value);
                n++; }
            else if(strcmp(param, "first_level_only") == 0) {
                if(strcmp(value, "true") == 0)
                    first_level_only = true;}
            break;
        }
        else if(c == '\\' && escape == 0 && quote_start == 1) // if escape character, set escape to 1
            escape = 1;
        else if(c == '\"' && quote_start == 0 && escape == 0) // if first quote, put subsequent characters into the param or value until second unescaped quote
            quote_start = 1;
        else if(c == '\"' && quote_start == 1 && escape == 0) // if second quote, then quote is closed
            quote_start = 0;
        else if(c == ' ' && quote_start == 0) // if space not inside quotes, this delimits end of name-value pair; process and break
        {
            value[j] = '\0';
            if(strcmp(param, "obj_paths") == 0)
            {
                strcpy(obj_paths[n], value);
                n++;
            }
            else if(strcmp(param, "first_level_only") == 0) {
                if(strcmp(value, "true") == 0)
                    first_level_only = true;}
            memset(param,0,PARAM_SIZE);
            memset(value,0,VALUE_SIZE);
            param_start = 1;
            value_start = 0;
            j = 0;
        }
        else if(c == ':' && quote_start == 0) // : delimits between param and value
        {
            param[j] = '\0';
            value_start = 1;
            param_start = 0;
            j = 0;
        }
        else if(param_start == 1) // everything else gets put into param or value depending on param_start and value_start
        {
            param[j] = c;
            j++;
        }
        else if(value_start == 1)
        {
            value[j] = c;
            escape = 0;
            j++;
        }
    }
    // put the message in the protobufs structure and send it
    mtp_send_to = InitializeMTPStructure();
    get_instances_message = CreateGetInstances(obj_paths, n, first_level_only);
    MSG_HANDLER_QueueMessage(agent_endpoint, get_instances_message, mtp_send_to);
    usp__msg__free_unpacked(get_instances_message, pbuf_allocator);
    return(0);
}

/*************************************************************************
**
** CreateGet
**
** Creates and populates the Get message structure
**
** \param   Array of requested paths
** \param   Number of requested paths
** \return  Pointer to a Get USP message structure
**          NOTE: If out of memory, USP Agent/Controller is terminated
**
**************************************************************************/
Usp__Msg *CreateGet(char p_paths[MAX_PATHS][VALUE_SIZE], int n)
{
    Usp__Msg *get_message;
    Usp__Header *header;
    Usp__Body *body;
    Usp__Request *request;
    Usp__Get *get_request;
    char *single_path;
    int x;

    // Allocate memory to store the USP message
    get_message = USP_MALLOC(sizeof(Usp__Msg));
    usp__msg__init(get_message);

    header = USP_MALLOC(sizeof(Usp__Header));
    usp__header__init(header);

    body = USP_MALLOC(sizeof(Usp__Body));
    usp__body__init(body);

    request = USP_MALLOC(sizeof(Usp__Request));
    usp__request__init(request);

    get_request = USP_MALLOC(sizeof(Usp__Get));
    usp__get__init(get_request);

    // Connect the structures together
    get_message->header = header;
    header->msg_id = USP_STRDUP(msg_id);
    header->msg_type = USP__HEADER__MSG_TYPE__GET;

    get_message->body = body;
    body->msg_body_case = USP__BODY__MSG_BODY_REQUEST;
    body->request = request;
    request->req_type_case = USP__REQUEST__REQ_TYPE_GET;
    // Point to and initialize the vector containing pointers to the requested paths
    request->get = get_request;
    get_request->n_param_paths = n;
    get_request->param_paths = NULL;
    get_request = get_message->body->request->get;
    get_request->param_paths = USP_REALLOC(get_request->param_paths, n*sizeof(void *));

    for(x=0;x<n;x++)
    {
        single_path = malloc(strlen(p_paths[x])+1);
        strcpy(single_path, p_paths[x]);
        get_request->param_paths[x] = USP_MALLOC(sizeof(p_paths[x]));
        get_request->param_paths[x] = USP_STRDUP(single_path);
    }
    return get_message;
}

/*************************************************************************
**
** CreateGetInstances
**
** Creates and populates the GetInstances message structure
**
** \param   Everything that goes into the message
** \return  Pointer to a GetInstances USP message structure
**          NOTE: If out of memory, USP Agent/Controller is terminated
**
**************************************************************************/
Usp__Msg *CreateGetInstances(char obj_paths[MAX_PATHS][VALUE_SIZE], int n, bool first_lvl_only)
{
    Usp__Msg *get_instances_message;
    Usp__Header *header;
    Usp__Body *body;
    Usp__Request *request;
    Usp__GetInstances *get_instances_request;
    char *single_path;
    int x;

    // Allocate memory to store the USP message
    get_instances_message = USP_MALLOC(sizeof(Usp__Msg));
    usp__msg__init(get_instances_message);

    header = USP_MALLOC(sizeof(Usp__Header));
    usp__header__init(header);

    body = USP_MALLOC(sizeof(Usp__Body));
    usp__body__init(body);

    request = USP_MALLOC(sizeof(Usp__Request));
    usp__request__init(request);

    get_instances_request = USP_MALLOC(sizeof(Usp__GetInstances));
    usp__get_instances__init(get_instances_request);

    // Connect the structures together
    get_instances_message->header = header;
    header->msg_id = USP_STRDUP(msg_id);
    header->msg_type = USP__HEADER__MSG_TYPE__GET_INSTANCES;

    get_instances_message->body = body;
    body->msg_body_case = USP__BODY__MSG_BODY_REQUEST;
    body->request = request;
    request->req_type_case = USP__REQUEST__REQ_TYPE_GET_INSTANCES;
    // Point to and initialize the vector containing pointers to the requested instances
    request->get_instances = get_instances_request;
    get_instances_request->n_obj_paths = n;
    get_instances_request->obj_paths = NULL;
    get_instances_request->first_level_only = first_lvl_only;
    get_instances_request = get_instances_message->body->request->get_instances;
    get_instances_request->obj_paths = USP_REALLOC(get_instances_request->obj_paths, n*sizeof(void *));

    for(x=0;x<n;x++) // populate values of requested instance names
    {
        single_path = malloc(strlen(obj_paths[x])+1);
        strcpy(single_path, obj_paths[x]);
        get_instances_request->obj_paths[x] = USP_MALLOC(sizeof(obj_paths[x]));
        get_instances_request->obj_paths[x] = USP_STRDUP(single_path);
    }
    return get_instances_message;
}
/*************************************************************************
**
** CreateGetSupportedDM
**
** Creates and populates GetSupportedDM message structure
**
** \param   Everything that goes into the message
** \return  Pointer to a GetSupportedDM USP message structure
**          NOTE: If out of memory, USP Agent/Controller is terminated
**
**************************************************************************/

Usp__Msg *CreateGetSupportedDM(char obj_paths[MAX_PATHS][VALUE_SIZE], int n, bool first_lvl_only, bool return_commands, bool return_events, bool return_params)
{
    Usp__Msg *get_supported_dm_message;
    Usp__Header *header;
    Usp__Body *body;
    Usp__Request *request;
    Usp__GetSupportedDM *get_supported_dm_request;
    char *single_path;
    int x;

    // Allocate memory to store the USP message
    get_supported_dm_message = USP_MALLOC(sizeof(Usp__Msg));
    usp__msg__init(get_supported_dm_message);

    header = USP_MALLOC(sizeof(Usp__Header));
    usp__header__init(header);

    body = USP_MALLOC(sizeof(Usp__Body));
    usp__body__init(body);

    request = USP_MALLOC(sizeof(Usp__Request));
    usp__request__init(request);

    get_supported_dm_request = USP_MALLOC(sizeof(Usp__GetSupportedDM));
    usp__get_supported_dm__init(get_supported_dm_request);

    // Connect the structures together
    get_supported_dm_message->header = header;
    header->msg_id = USP_STRDUP(msg_id);
    header->msg_type = USP__HEADER__MSG_TYPE__GET_SUPPORTED_DM;

    get_supported_dm_message->body = body;
    body->msg_body_case = USP__BODY__MSG_BODY_REQUEST;
    body->request = request;
    request->req_type_case = USP__REQUEST__REQ_TYPE_GET_SUPPORTED_DM;
    request->get_supported_dm = get_supported_dm_request;
    // Point to and initialize the vector containing pointers to the requested path info
    get_supported_dm_request->n_obj_paths = n;
    get_supported_dm_request->obj_paths = NULL;
    get_supported_dm_request->first_level_only = first_lvl_only;
    get_supported_dm_request->return_commands = return_commands;
    get_supported_dm_request->return_events = return_events;
    get_supported_dm_request->return_params = return_params;
    get_supported_dm_request = get_supported_dm_message->body->request->get_supported_dm;
    get_supported_dm_request->obj_paths = USP_REALLOC(get_supported_dm_request->obj_paths, n*sizeof(void *));

    for(x=0;x<n;x++) // populate the values of the requested path names
    {
        single_path = malloc(strlen(obj_paths[x])+1);
        strcpy(single_path, obj_paths[x]);
        get_supported_dm_request->obj_paths[x] = USP_MALLOC(sizeof(obj_paths[x]));
        get_supported_dm_request->obj_paths[x] = USP_STRDUP(single_path);
    }
    return get_supported_dm_message;
}
/*************************************************************************
**
** CreateAdd
**
** Creates and populates Add message structure
**
** \param   Everything that goes into the message
** \return  Pointer to an Add USP message structure
**          NOTE: If out of memory, USP Agent/Controller is terminated
**
**************************************************************************/
Usp__Msg *CreateAdd(bool allow_partial, char obj_paths[MAX_PATHS][VALUE_SIZE], bool req[MAX_PATHS][MAX_PARAMVALUE], int n_p_settings[MAX_PATHS], char p_settings_param[MAX_PATHS][MAX_PARAMVALUE][PARAM_SIZE], char p_settings_value[MAX_PATHS][MAX_PARAMVALUE][VALUE_SIZE], int n)
{
    Usp__Msg *add_message;
    Usp__Header *header;
    Usp__Body *body;
    Usp__Request *request;
    Usp__Add *add_request;
    Usp__Add__CreateObject *create_obj;
    Usp__Add__CreateParamSetting *setting;
    char *single_path;
    char *single_param;
    char *single_value;
    int x, y;

    // Allocate memory to store the USP message
    add_message = USP_MALLOC(sizeof(Usp__Msg));
    usp__msg__init(add_message);

    header = USP_MALLOC(sizeof(Usp__Header));
    usp__header__init(header);

    body = USP_MALLOC(sizeof(Usp__Body));
    usp__body__init(body);

    request = USP_MALLOC(sizeof(Usp__Request));
    usp__request__init(request);

    add_request = USP_MALLOC(sizeof(Usp__Add));
    usp__add__init(add_request);

    // Connect the structures together
    add_message->header = header;
    header->msg_id = USP_STRDUP(msg_id);
    header->msg_type = USP__HEADER__MSG_TYPE__ADD;

    add_message->body = body;
    body->msg_body_case = USP__BODY__MSG_BODY_REQUEST;
    body->request = request;
    request->req_type_case = USP__REQUEST__REQ_TYPE_ADD;
    // Point to and initialize the vector containing pointers to the object instances to update
    request->add = add_request;
    add_request->n_create_objs = n;
    add_request->allow_partial = allow_partial;
    add_request->create_objs = USP_REALLOC(add_request->create_objs, n*sizeof(void *));

    for(x=0;x<n;x++) // populate the object names to add instances to
    {
        create_obj = USP_MALLOC(sizeof(Usp__Add__CreateObject));
        usp__add__create_object__init(create_obj);
        add_message->body->request->add->create_objs[x] = create_obj;
        create_obj->n_param_settings = n_p_settings[x];
        single_path = malloc(strlen(obj_paths[x])+1);
        strcpy(single_path, obj_paths[x]);
        create_obj->obj_path = USP_STRDUP(single_path);
        create_obj->param_settings = USP_REALLOC(create_obj->param_settings, create_obj->n_param_settings*sizeof(void *));
	// populate the set of parameter/value pairs to include in the added object instance
        for(y=0;y<n_p_settings[x];y++)
        {
            setting = USP_MALLOC(sizeof(Usp__Add__CreateParamSetting));
            usp__add__create_param_setting__init(setting);
            add_message->body->request->add->create_objs[x]->param_settings[y] = setting;
            single_param = malloc(strlen(p_settings_param[x][y])+1);
            strcpy(single_param, p_settings_param[x][y]);
            setting->param = USP_MALLOC(sizeof(p_settings_param[x][y]));
            setting->param = USP_STRDUP(single_param);
            single_value = malloc(strlen(p_settings_value[x][y])+1);
            strcpy(single_value, p_settings_value[x][y]);
            setting->value = USP_MALLOC(sizeof(p_settings_value[x][y]));
            setting->value = USP_STRDUP(single_value);
            setting->required = req[x][y];
        }
    }
    return add_message;
}
/*************************************************************************
**
** CreateOperate
**
** Creates and populates Operate message structure
**
** \param   Everything that goes into the message
** \return  Pointer to an Operate USP message structure
**          NOTE: If out of memory, USP Agent/Controller is terminated
**
**************************************************************************/
Usp__Msg *CreateOperate(char command[VALUE_SIZE], char command_key[VALUE_SIZE], char keys[MAX_PARAMVALUE][PARAM_SIZE], char values[MAX_PARAMVALUE][VALUE_SIZE], bool send_resp, int n)
{
    Usp__Msg *operate_message;
    Usp__Header *header;
    Usp__Body *body;
    Usp__Request *request;
    Usp__Operate *operate_request;
    Usp__Operate__InputArgsEntry *input_arg;
    char *single_key;
    char *single_value;
    int x;

    // Allocate memory to store the USP message
    operate_message = USP_MALLOC(sizeof(Usp__Msg));
    usp__msg__init(operate_message);

    header = USP_MALLOC(sizeof(Usp__Header));
    usp__header__init(header);

    body = USP_MALLOC(sizeof(Usp__Body));
    usp__body__init(body);

    request = USP_MALLOC(sizeof(Usp__Request));
    usp__request__init(request);

    operate_request = USP_MALLOC(sizeof(Usp__Operate));
    usp__operate__init(operate_request);

    // Connect the structures together
    operate_message->header = header;
    header->msg_id = USP_STRDUP(msg_id);
    header->msg_type = USP__HEADER__MSG_TYPE__OPERATE;

    operate_message->body = body;
    body->msg_body_case = USP__BODY__MSG_BODY_REQUEST;
    body->request = request;
    request->req_type_case = USP__REQUEST__REQ_TYPE_OPERATE;
    // Point to and initialize the vector containing pointers to the object instances to update
    request->operate = operate_request;
    operate_request->command = USP_STRDUP(command);
    operate_request->command_key = USP_STRDUP(command_key);
    operate_request->send_resp = send_resp;
    operate_request->n_input_args = n;
    operate_request->input_args = USP_REALLOC(operate_request->input_args, n*sizeof(void *));

    for(x=0;x<n;x++) // populate the object names to add instances to
    {
        input_arg = USP_MALLOC(sizeof(Usp__Operate__InputArgsEntry));
        usp__operate__input_args_entry__init(input_arg);
        operate_message->body->request->operate->input_args[x] = input_arg;
        single_key = malloc(strlen(keys[x])+1);
        strcpy(single_key, keys[x]);
        input_arg->key = USP_STRDUP(single_key);
        single_value = malloc(strlen(values[x])+1);
        strcpy(single_value, values[x]);
        input_arg->value = USP_STRDUP(single_value);
    }
    return operate_message;
}
/*************************************************************************
**
** CreateSet
**
** Creates and populates the Set message structure
**
** \param   Everything that goes into the message
** \return  Pointer to a Set USP message structure
**          NOTE: If out of memory, USP Agent/Controller is terminated
**
**************************************************************************/
Usp__Msg *CreateSet(bool allow_partial, char obj_paths[MAX_PATHS][VALUE_SIZE], bool req[MAX_PATHS][MAX_PARAMVALUE], int n_p_settings[MAX_PATHS], char p_settings_param[MAX_PATHS][MAX_PARAMVALUE][PARAM_SIZE], char p_settings_value[MAX_PATHS][MAX_PARAMVALUE][VALUE_SIZE], int n)
{
    Usp__Msg *set_message;
    Usp__Header *header;
    Usp__Body *body;
    Usp__Request *request;
    Usp__Set *set_request;
    Usp__Set__UpdateObject *update_obj;
    Usp__Set__UpdateParamSetting *setting;
    char *single_path;
    char *single_param;
    char *single_value;
    int x, y;

    // Allocate memory to store the USP message
    set_message = USP_MALLOC(sizeof(Usp__Msg));
    usp__msg__init(set_message);

    header = USP_MALLOC(sizeof(Usp__Header));
    usp__header__init(header);

    body = USP_MALLOC(sizeof(Usp__Body));
    usp__body__init(body);

    request = USP_MALLOC(sizeof(Usp__Request));
    usp__request__init(request);

    set_request = USP_MALLOC(sizeof(Usp__Set));
    usp__set__init(set_request);

    // Connect the structures together
    set_message->header = header;
    header->msg_id = USP_STRDUP(msg_id);
    header->msg_type = USP__HEADER__MSG_TYPE__SET;

    set_message->body = body;
    body->msg_body_case = USP__BODY__MSG_BODY_REQUEST;
    body->request = request;
    request->req_type_case = USP__REQUEST__REQ_TYPE_SET;
    // Point to and initialize the vector containing pointers to the object instances to update
    request->set = set_request;
    set_request->n_update_objs = n;
    set_request->update_objs = NULL;
    set_request->allow_partial = allow_partial;
    set_request->update_objs = USP_REALLOC(set_request->update_objs, n*sizeof(void *));

    for(x=0;x<n;x++) // populate the object names to be updated
    {
        update_obj = USP_MALLOC(sizeof(Usp__Set__UpdateObject));
        usp__set__update_object__init(update_obj);
        set_message->body->request->set->update_objs[x] = update_obj;
        update_obj->n_param_settings = n_p_settings[x];
        single_path = malloc(strlen(obj_paths[x])+1);
        strcpy(single_path, obj_paths[x]);
        update_obj->obj_path = USP_STRDUP(single_path);
        update_obj->param_settings = USP_REALLOC(update_obj->param_settings, update_obj->n_param_settings*sizeof(void *));
        // populate the set of parameter/value pairs to update
        for(y=0;y<n_p_settings[x];y++)
        {
            setting = USP_MALLOC(sizeof(Usp__Set__UpdateParamSetting));
            usp__set__update_param_setting__init(setting);
            set_message->body->request->set->update_objs[x]->param_settings[y] = setting;
            single_param = malloc(strlen(p_settings_param[x][y])+1);
            strcpy(single_param, p_settings_param[x][y]);
            setting->param = USP_MALLOC(sizeof(p_settings_param[x][y]));
            setting->param = USP_STRDUP(single_param);
            single_value = malloc(strlen(p_settings_value[x][y])+1);
            strcpy(single_value, p_settings_value[x][y]);
            setting->value = USP_MALLOC(sizeof(p_settings_value[x][y]));
            setting->value = USP_STRDUP(single_value);
            setting->required = req[x][y];
        }
    }
    return set_message;
}
/*************************************************************************
**
** CreateGetSupportedProtocol
**
** Creates and populates the GetGetSupportedProtocol message structure
**
** \param   Protocol version of Controller implementation (from file)
** \return  Pointer to a GetGetSupportedProtocol USP message structure
**          NOTE: If out of memory, USP Agent/Controller is terminated
**
**************************************************************************/

Usp__Msg *CreateGetSupportedProtocol(char version[VALUE_SIZE])
{
    Usp__Msg *get_supported_protocol_message;
    Usp__Header *header;
    Usp__Body *body;
    Usp__Request *request;
    Usp__GetSupportedProtocol *get_supported_protocol_request;

    // Allocate memory to store the USP message
    get_supported_protocol_message = USP_MALLOC(sizeof(Usp__Msg));
    usp__msg__init(get_supported_protocol_message);

    header = USP_MALLOC(sizeof(Usp__Header));
    usp__header__init(header);

    body = USP_MALLOC(sizeof(Usp__Body));
    usp__body__init(body);

    request = USP_MALLOC(sizeof(Usp__Request));
    usp__request__init(request);

    get_supported_protocol_request = USP_MALLOC(sizeof(Usp__GetSupportedProtocol));
    usp__get_supported_protocol__init(get_supported_protocol_request);

    // Connect the structures together
    get_supported_protocol_message->header = header;
    header->msg_id = USP_STRDUP(msg_id);
    header->msg_type = USP__HEADER__MSG_TYPE__GET_SUPPORTED_PROTO;

    get_supported_protocol_message->body = body;
    body->msg_body_case = USP__BODY__MSG_BODY_REQUEST;
    body->request = request;
    request->req_type_case = USP__REQUEST__REQ_TYPE_GET_SUPPORTED_PROTOCOL;
    request->get_supported_protocol = get_supported_protocol_request;
    get_supported_protocol_request->controller_supported_protocol_versions = USP_STRDUP(version);

    return get_supported_protocol_message;
}

/*************************************************************************
**
** CreateDelete
**
** Creates and populates the GetDelete message structure
**
** \param   Object paths to delete and the number of object paths
** \return  Pointer to a GetDelete USP message structure
**          NOTE: If out of memory, USP Agent/Controller is terminated
**
**************************************************************************/
Usp__Msg *CreateDelete(char obj_paths[MAX_PATHS][VALUE_SIZE], int n)
{
    Usp__Msg *delete_message;
    Usp__Header *header;
    Usp__Body *body;
    Usp__Request *request;
    Usp__Delete *delete_request;
    char *single_path;
    int x;

    // Allocate memory to store the USP message
    delete_message = USP_MALLOC(sizeof(Usp__Msg));
    usp__msg__init(delete_message);

    header = USP_MALLOC(sizeof(Usp__Header));
    usp__header__init(header);

    body = USP_MALLOC(sizeof(Usp__Body));
    usp__body__init(body);

    request = USP_MALLOC(sizeof(Usp__Request));
    usp__request__init(request);

    delete_request = USP_MALLOC(sizeof(Usp__Delete));
    usp__delete__init(delete_request);

    // Connect the structures together
    delete_message->header = header;
    header->msg_id = USP_STRDUP(msg_id);
    header->msg_type = USP__HEADER__MSG_TYPE__DELETE;

    delete_message->body = body;
    body->msg_body_case = USP__BODY__MSG_BODY_REQUEST;
    body->request = request;
    request->req_type_case = USP__REQUEST__REQ_TYPE_DELETE;
    // Point to and initialize the vector containing pointers to the objects to be deleted
    request->delete_ = delete_request;
    delete_request->n_obj_paths = n;
    delete_request->obj_paths = NULL;
    delete_request = delete_message->body->request->delete_;
    delete_request->obj_paths = USP_REALLOC(delete_request->obj_paths, n*sizeof(void *));

    for(x=0;x<n;x++) // populate the values of the object paths to delete
    {
        single_path = malloc(strlen(obj_paths[x])+1);
        strcpy(single_path, obj_paths[x]);
        delete_request->obj_paths[x] = USP_MALLOC(sizeof(obj_paths[x]));
        delete_request->obj_paths[x] = USP_STRDUP(single_path);
    }
    return delete_message;
}

/***********************************************************************
**
** Initialize mtp_send_to parameter
**
** Sets values in the mtp_send_to structure, as determined by first line of file
**
** \return  pointer to mtp_send_to structure 
**
**************************************************************************/
mtp_reply_to_t *InitializeMTPStructure(void)
{
    // Allocate memory to store MTP info

    mtp_send.stomp_instance = stomp_instance;
    mtp_send.stomp_dest = stomp_dest;
    mtp_send.coap_host = coap_host;
    mtp_send.coap_port = coap_port;
    mtp_send.coap_resource = coap_resource;
    if (strlen(mtp_send.stomp_dest) > 0) {
        mtp_send.protocol = kMtpProtocol_STOMP; }
    else if (mtp_send.coap_port > 0) {
        mtp_send.protocol = kMtpProtocol_CoAP; }
    mtp_send.is_reply_to_specified = true;
    return &mtp_send;
}

/***********************************************************************
**
** StartBasicAgentProcesses
** [this method was copied from main.c]
**
** Starts Agent processes so they can be used to send messages
**
** \return  USP error message
**
**************************************************************************/
int StartBasicAgentProcesses(void)
{
    int err;
    char *db_file = DEFAULT_DATABASE_FILE;
    bool enable_mem_info = false;

    // Sleep until other services which USP Agent processes use (eg DNS) are running
    // (ideally USP Agent should be started when the services are running, rather than sleeping here. But sometimes, there is no easy way to ensure this).
    if (DAEMON_START_DELAY_MS > 0)
    {
        usleep(DAEMON_START_DELAY_MS*1000);
    }

    // Exit if unable to start USP Agent processes
    err = Controller_Start(db_file, enable_mem_info);
    if (err != USP_ERR_OK)
    {
        goto exit;
    }

    // Exit if unable to spawn off a thread to service the MTPs
    err = OS_UTILS_CreateThread(MTP_EXEC_StompMain, NULL);
    if (err != USP_ERR_OK)
    {
        goto exit;
    }

#ifdef ENABLE_COAP
    err = OS_UTILS_CreateThread(MTP_EXEC_CoapMain, NULL);
    if (err != USP_ERR_OK)
    {
        goto exit;
    }
#endif

    // Not bothering with bulk data set up; just return
    // Exit if unable to spawn off a thread to perform bulk data collection posts
    err = OS_UTILS_CreateThread(BDC_EXEC_Main, NULL);
    if (err != USP_ERR_OK)
    {
        goto exit;
    }

    err = 0;
    return(err);

exit:
    // If the code gets here, an error occurred
    USP_LOG_Error("USP Agent aborted unexpectedly");
    return -1;
}
/*************************************************************************
**
** Controller_Start
**
** Initializes and starts USP Agent, to set up connections
** [this method was copied from main.c, omitting endless loop at end of main.c method]
**
** \param   db_file - pointer to name of USP Agent's database file to open
** \param   enable_mem_info - Set to true if memory debugging info should be collected
**
** \return  USP_ERR_OK if successful
**
**************************************************************************/
int Controller_Start(char *db_file, bool enable_mem_info)
{
    CURLcode curl_err;
    int err;

    // Exit if unable to initialise libraries which need to be initialised when running single threaded
    curl_err = curl_global_init(CURL_GLOBAL_ALL);
    if (curl_err != 0)
    {
        USP_LOG_Error("%s: curl_global_init() failed (curl_err=%d)", __FUNCTION__, curl_err);
        return USP_ERR_INTERNAL_ERROR;
    }

    SYNC_TIMER_Init();

    // Turn off SIGPIPE, since we use non-blocking connections and would prefer to get the EPIPE error
    // NOTE: If running USP Agent in GDB: GDB ignores this code and will still generate SIGPIPE
    signal(SIGPIPE, SIG_IGN);

#ifdef ENABLE_HIDL
    // Exit if unable to start the Android HIDL server
    err = HIDL_SERVER_Start();
    if (err != USP_ERR_OK)
    {
        return err;
    }
#endif

    // Exit if an error occurred when initialising the database
    err = DATABASE_Init(db_file);
    if (err != USP_ERR_OK)
    {
        return err;
    }

    // Exit if an error occurred when initialising any of the the message queues used by the threads
    err = DM_EXEC_Init();
    err |= MTP_EXEC_Init();
    err |= BDC_EXEC_Init();
    if (err != USP_ERR_OK)
    {
        return err;
    }

    // Initialise the random number generator seeds
    RETRY_WAIT_Init();

    // Exit if unable to add all schema paths to the data model
    err = DATA_MODEL_Init();
    if (err != USP_ERR_OK)
    {
        return err;
    }

    // Start logging memory usage from now on (since the static data model schema allocations have completed)
    if (enable_mem_info)
    {
        USP_MEM_StartCollection();
    }

    // Exit if unable to start the datamodel objects
    err = DATA_MODEL_Start();
    if (err != USP_ERR_OK)
    {
        return err;
    }

    // Start the STOMP connections. This must be done here, before other parts of the data model that require stomp connections
    // to queue messages (eg object creation/deletion notifications)
    err = DEVICE_STOMP_StartAllConnections();
    if (err != USP_ERR_OK)
    {
        return err;
    }

    return USP_ERR_OK;
}

/*************************************************************************
**
** CTRL_FILE_PARSER_Start
**
** Called from main.c when -x (controller mode) option is in command line
**
** \param  controller_file that contains lines of Controller messages to send
** \return  USP error message
**
**************************************************************************/
int CTRL_FILE_PARSER_Start(char *controller_file)
{
    int i = 0;
    int err;
    char line[LINE_SIZE];
    FILE *fp;

    err = StartBasicAgentProcesses();
    fp = fopen(controller_file,"r"); // open the file
    if( fp == NULL ) // make sure the file actually exists
    {
        printf("Cannot find %s\n", controller_file);
        return(1);
    }

    while (fgets(line, sizeof(line), fp))
    {
        if(i == 0)
        {
            ParseFirstLine(line);
            i++;
        }
        else
        {
            // wait so we don't overrun Agent buffer or close connections before messages sent
            sleep(WAIT_BETWEEN_MSGS);
            ReadMessageType(line); // determine what USP message type the line is
            if(strcmp(line_msg_type, "Get") == 0)
                ParseGet(line);
            if(strcmp(line_msg_type, "GetSupportedDM") == 0)
                ParseGetSupportedDM(line);
            if(strcmp(line_msg_type, "Add") == 0)
                ParseAdd(line);
            if(strcmp(line_msg_type, "Set") == 0)
                ParseSet(line);
            if(strcmp(line_msg_type, "Delete") == 0)
                ParseDelete(line);
            if(strcmp(line_msg_type, "Operate") == 0)
                ParseOperate(line);
            if(strcmp(line_msg_type, "GetInstances") == 0)
                ParseGetInstances(line);
            if(strcmp(line_msg_type, "GetSupportedProtocol") == 0)
                ParseGetSupportedProtocol(line);
            snprintf(msg_id, strlen(msg_id)+1, "%i", atoi((void *)msg_id)+1);
            i++;
        }
    }

    sleep(2*WAIT_BETWEEN_MSGS); // wait before closing to make sure all messages were sent
    /* close */
    fclose(fp);
    MAIN_Stop();
    return(err);
}
