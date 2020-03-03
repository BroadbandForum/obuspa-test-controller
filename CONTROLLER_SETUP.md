# Controller Setup

## Installing and Configuring the OB-USP-Agent
The test controller is nothing more than the OB-USP-Agent with an added function capable of parsing an input file and creating Controller USP Messages from the input file. OB-USP-Agent functions are invoked for setting up connections, formatting and sending messages. To install and initially configure the test controller, follow the instructions for installing the OB-USP-Agent in QUICK_START_GUIDE.md. This test controller can be run as an Agent, and all command line options (described in QUICK_START_GUIDE.md) are available.

## Setting up the Database 
Once the code is installed, the database needs to be configured. The database is an Agent database. For the purposes of setting up connections and sending USP Messages, it is important to recognize this test controller thinks of itself as an Agent. The Agent processes being used to establish connections and send messages are completely unaware that they are being invoked for the purpose of sending Controller messages.

The database will need parameters set as described in the following sub-sections. It may be useful to create a script that sets all these parameters using the syntax:
```
obuspa -c dbset "<parameter path and name>" "parameter value"
```
for each parameter being set (one line per parameter).

### Endpoint ID

You may want to give your test controller an easy-to-remember-and-recognize Endpoint ID. For example, you could give it an Endpoint ID of "proto::ControllerA".
```
obuspa -c dbset "Device.LocalAgent.EndpointID" "proto::ControllerA"
```

### STOMP
If you will be using STOMP, configure (in your STOMP server) username, password, and a destination for this test controller. Configure the database STOMP parameters. If the STOMP Connection or LocalAgent.MTP STOMP instances do not exist in the database, you may need to add them, first, using the `obuspa -c dbadd "<object name and instance>"` command. Make sure there is an Alias in the object, if you add a new instance.
```
obuspa -c dbadd "Device.STOMP.Connection.<instance number>."
obuspa -c dbset "Device.STOMP.Connection.<instance number>.Host" "<IP address or FQDN of STOMP server>"
obuspa -c dbset "Device.STOMP.Connection.<instance number>.Port" "<port STOMP server is listening on>"
obuspa -c dbset "Device.STOMP.Connection.<instance number>.Username" "<username configured for this test controller in the STOMP server>"
obuspa -c dbset "Device.STOMP.Connection.<instance number>.Password" "<the password configured in the STOMP server>"
obuspa -c dbset "Device.STOMP.Connection.<instance number>.Port" "<port STOMP server is listening on>"
obuspa -c dbset "Device.STOMP.Connection.<instance number>.Enable" "true"
obuspa -c dbset "Device.LocalAgent.MTP.<instance number>.Protocol" "STOMP"
obuspa -c dbset "Device.LocalAgent.MTP.<instance number>.Enable" "true"
obuspa -c dbset "Device.LocalAgent.MTP.<instance number>.STOMP.Destination" "<the destination this test controller is expected to subscribe to for receiving USP messages>"
```

If you want to disable encryption while testing, that can be done through the vendor extension parameter X_ARRIS-COM_EnableEcryption.
```
obuspa -c dbset "Device.STOMP.Connection.<instance number>.X_ARRIS-COM_EnableEncryption" "false"
```

## CoAP
If you will be using CoAP, this controller will need to know its CoAP server port and resource name. Configure the database CoAP parameters. If the LocalAgent.MTP CoAP instance does not exist in the database, you may need to add it, first, using the `obuspa -c dbadd "<object name and instance>"` command. Make sure there is an Alias in the object, if you add a new instance.
```
obuspa -c dbset "Device.LocalAgent.MTP.2.Protocol" "CoAP"
obuspa -c dbset "Device.LocalAgent.MTP.2.Enable" "true"
obuspa -c dbset "Device.LocalAgent.MTP.2.CoAP.EnableEncryption" "[true|false]"
obuspa -c dbset "Device.LocalAgent.MTP.2.CoAP.Interfaces" "<comma delimited list of interface names to run CoAP over>"
obuspa -c dbset "Device.LocalAgent.MTP.2.CoAP.Path" "<name of CoAP resource -- any word will do, like "Controller">"
obuspa -c dbset "Device.LocalAgent.MTP.2.CoAP.Port" "5683" // recommend just using the default port of 5683
```

## Configuring Information about Agents
The test controller needs to know about Agents it will be sending messages to. These Agents need to be in the test controller's Device.LocalAgent.Controller table -- because the test controller thinks it is an Agent and the Endpoints it talks to are Controllers. 
For general configuration, an instance is needed in the Controller table with Alias, EndpointID and Enabled. If you need to add an instance, use `obuspa -c dbadd "<object name and instance>"` command. Make sure there is an Alias in the object, if you add a new instance. You can use an `obuspa -c dbset "Device.LocalAgent.Controller.<instance>.Alias" "<alias name>"` command to set the Alias.

```
obuspa -c dbset "Device.LocalAgent.Controller.<instance number>.EndpointID" "<Agent Endpoint ID>"
obuspa -c dbset "Device.LocalAgent.Controller.<instance number>.Enable" "true"
```

For the STOMP parameters, you may need to add an MTP instance, with Alias. And then set:
```
obuspa -c dbset "Device.LocalAgent.Controller.<instance number>.MTP.<instance number>.STOMP.Destination" "<STOMP destination the Agent subscribes to>"
obuspa -c dbset "Device.LocalAgent.Controller.<instance number>.MTP.<instance number>.Enable" "true"
```

For the CoAP parameters, you may need to add an MTP instance, with Alias. And then set:
```
obuspa -c dbset "Device.LocalAgent.Controller.1.MTP.2.Protocol" "CoAP"
obuspa -c dbset "Device.LocalAgent.Controller.1.MTP.2.CoAP.EnableEncryption" "false" // if certs aren't set up
obuspa -c dbset "Device.LocalAgent.Controller.1.MTP.2.CoAP.Host" "<IP address or FQDN of Agent>"
obuspa -c dbset "Device.LocalAgent.Controller.1.MTP.2.CoAP.Path" "<name of CoAP resource -- any word will do, like "Agent">"
obuspa -c dbset "Device.LocalAgent.Controller.1.MTP.2.CoAP.Port" "5683" // recommend using default port of 5683
obuspa -c dbset "Device.LocalAgent.Controller.1.MTP.2.Enable" "true"
```

## Setting up Agents to Work with this Controller
Once you know the Endpoint ID and connectivity information of this Controller, you'll need to create Controller table entries for it in all the Agents you'll be testing it against. 

For general configuration, an instance is needed in the Controller table with Alias, EndpointID and Enabled. If you need to add an instance, use `obuspa -c dbadd "<object name and instance>"` command. Make sure there is an Alias in the object, if you add a new instance. You can use an `obuspa -c dbset "Device.LocalAgent.Controller.<instance>.Alias" "<alias name>"` command to set the Alias.

```
obuspa -c dbset "Device.LocalAgent.Controller.<instance number>.EndpointID" "<test controller Endpoint ID>"
obuspa -c dbset "Device.LocalAgent.Controller.<instance number>.Enable" "true"
```

For the STOMP parameters, you may need to add an MTP instance, with Alias. And then set:
```
obuspa -c dbset "Device.LocalAgent.Controller.<instance number>.MTP.<instance number>.STOMP.Destination" "<STOMP destination the test controller subscribes to>"
obuspa -c dbset "Device.LocalAgent.Controller.<instance number>.MTP.<instance number>.Enable" "true"
```

For the CoAP parameters, you may need to add an MTP instance, with Alias. And then set:
```
obuspa -c dbset "Device.LocalAgent.Controller.1.MTP.2.Protocol" "CoAP"
obuspa -c dbset "Device.LocalAgent.Controller.1.MTP.2.CoAP.EnableEncryption" "false" // if certs aren't set up
obuspa -c dbset "Device.LocalAgent.Controller.1.MTP.2.CoAP.Host" "<IP address or FQDN of test controller>"
obuspa -c dbset "Device.LocalAgent.Controller.1.MTP.2.CoAP.Path" "<name of CoAP resource -- the same word you gave the test controller>"
obuspa -c dbset "Device.LocalAgent.Controller.1.MTP.2.CoAP.Port" "5683" // recommend using default port of 5683
obuspa -c dbset "Device.LocalAgent.Controller.1.MTP.2.Enable" "true"
```

And remember that the Agent needs its MTPs set up. Make sure whatever value you told the test controller for an Agent's STOMP destination or CoAP resource is set identically in the Agent's Device.LocalAgent.MTP instance.

## Controller Test File

See the CTRL_FILE_RULES.md and example files for creating your own test files.

## Running the Test Controller

First start the Agent. You can use `obuspa -p -v 4` to run the Agent. You may want to redirect its output into a file.

To run the test controller, you can use `obuspa -p -v 4 -x <test file name>`. Note that the Agent replies are only seen in the Agent output, unless you use a packet capture tool, like Wireshark, to see them at the controller.
