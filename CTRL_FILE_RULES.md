# File Message Syntax and Usage

Examples are given in those files:
- ctrl_stomp_example.txt
- ctrl_coap_example.txt
- ctrl_mqtt_example.txt

## General syntax
- The file parser ignores lines when starting with the hash mark (#).
- The file parser also ignores empty lines (e.g. starting with LF or CR control character).
- Each message must fit on a single line.

## The first line
The first line declares the USP and MTP parameters used for messages to be sent, so it MUST have the parameters correctly declared.

### USP Message ID
The first line MUST have the `msg_id:"<integer>"` parameter. This value will be used for the first message being sent. Sequential numbers (incremented by 1) will be used for subsequent messages in the same file.

### Endpoint ID of the destination
The first line MUST have the `to_id:"<string>"` parameter. This value will be used for all messages being sent. That identifier is the `Device.LocalAgent.EndpointID` value of the Agent-to-be-communicated-with.

### MTP parameters
The first line MUST also include the parameters for one of the MTP to be used:

If STOMP, then include:
- `stomp_agent_dest:"<destination of the Agent-to-be-communicated-with>"`
- `stomp_instance:"<Device.STOMP.Connection. instance number this test controller will use to connect to the STOMP server>"`

If CoAP, then include:
- `coap_host:"<IP address or FQDN of the AGENT>"`
- `coap_port:"<IANA assigned default value is 5683>"`
- `coap_resource:"<the same as the Agent-to-be-communicated-with Device.LocalAgent.MTP.{i}.CoAP.Resource>"`

If MQTT, then include:
- `mqtt_topic:"<topic of the Agent-to-be-communicated-with has subscribed to>"`
- `mqtt_instance:"<instance number of the Device.MQTT.Client this test controller will use to connect to the MQTT broker>"`

You can use the `obuspa -c show database` command (on this test controller and on the Agent-to-be-communicated-with) to determine the MTP parameter values that need to be included in the first line.

## The other lines
The Controller uses a waiting time specified by the hardcoded `WAIT_BETWEEN_MSGS` variable declared in the `src/vendor/ctrl_file_parser.c` file. That waiting time is in seconds and is applied:
- before sending the first message,
- between all sending, and
- after sending the last message.

The Controller process will end after last message in a file is sent.

### Controller messages
Parameters with default values can be omitted.

Supported parameters, per msg_type, are:

- `Get`:
  - `param_paths` (required, may be repeated)
- `GetSupportedDM`:
  - `obj_paths` (required, may be repeated),
  - `first_level_only` (default=false),
  - `return_commands` (default=false),
  - `return_events` (default=false),
  - `return_params` (default=false)
- `GetInstances`:
  - `obj_paths` (required, may be repeated),
  - `first_level_only` (default=false)
- `Set`:
  - `allow_partial` (default=false),
  - `update_objs` (required, may be repeated, expressed as `obj_path` with one or more param and value parameter pairs, each pair enclosed in "{}"),
  - `required` (default=false)
- `Add`:
  - `allow_partial` (default=false),
  - `create_objs` (required, may be repeated, expressed as `obj_path` with one or more param and value parameter pairs, each pair enclosed in "{}"),
  - `required` (default=false)
- `Delete`:
  - `obj_paths` (required, may be repeated)
- `Operate`:
  - `command` (required),
  - `command_key` (required),
  - `send_resp` (default=false),
  - with zero or more param and value parameter pairs, each pair enclosed in "{}"
- `GetSupportedProtocol`:
  - `controller_supported_protocol_versions` (required)

## Unsupported feature
- There is no output of received USP messages.
- There is no support for NotifyResp message.
