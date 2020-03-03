# File Message Syntax and Usage

The first line of the file MUST have "msg_id:"\<integer\>".  This value will be used for the first message being sent. Sequential numbers (incremented by 1) will be used for subsequent messages in the same file.

The first line MUST also include parameters for the MTP to be used:

- If STOMP, then include stomp_agent_dest:"\<destination of the Agent-to-be-communicated-with\>" and stomp_instance:"\<Device.STOMP.Connection. instance number this test controller will use to connect to the STOMP server\>"
- If CoAP, then include coap_host:"\<IP address or FQDN of the AGENT\>", coap_port:"\<IANA assigned default value is  5683\>" coap_resource:"\<the same as the Agent-to-be-communicated-with Device.LocalAgent.MTP.{i}.CoAP.Resource\>".

You can use the `obuspa -c show database` command (on this test controller and on the Agent-to-be-communicated-with) to determine the MTP parameter values that need to be included in the first line.

The Controller will wait the number of seconds specified by the WAIT_BETWEEN_MSGS variable between sending each message.

The Controller process will end after last message in a file is sent.

Parameters with default values can be omitted.

Supported parameters, per msg_type, are:

- Get: param_paths (required, may be repeated)
- GetSupportedDM: obj_paths (required, may be repeated), first_level_only (default=false), return_commands (default=false), return_events (default=false), return_params (default=false)
- GetInstances: obj_paths (required, may be repeated), first_level_only (default=false)
- Set: allow_partial (default=false), update_objs (required, may be repeated, expressed as obj_path and one or more param and value parameter pairs, each pair enclosed in "{}"), obj_path (required inside each create_objs), param and value pair enclosed in "{}" (required inside each obj_path), required (default=false)
- Add: allow_partial (default=false), create_objs (required, may be repeated, expressed as obj_path and one or more param and value parameter pairs, each pair enclosed in "{}"), obj_path (required inside each create_objs), param and value pair enclosed in "{}" (required inside each obj_path), required (default=false)
- Delete: allow_partial (default=false), obj_paths (required, may be repeated)
- Operate: command (required), command_key (required), send_resp (default=false), zero or more param and value parameter pairs, each pair enclosed in "{}" 
- GetSupportedProtocol: controller_supported_protocol_versions (required) 

There is currently no support for NotifyResp. 

