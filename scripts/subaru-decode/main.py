import argparse
import pandas as pd
import yaml

from enum import Enum
from typing import Dict, Iterator, Optional, TypedDict, cast


class ModuleID(int, Enum):
    ECU_REQ = 0x7E0
    ECU_RES = 0x7E8
    ABS_VDC_REQ = 0x7B0
    ABS_VDC_RES = 0x7B8


"""
memory addr reference
"""


class AddressValue(TypedDict):
    unit: str
    expr: str

class AddressInfo(TypedDict):
    name: str
    length: int
    value: AddressValue

def init_reference() -> Dict[str, Dict[int, AddressInfo]]:
    with open("known-addresses.yaml", "r") as f:
        return cast(Dict[str, Dict[int, AddressInfo]], yaml.safe_load(f))


"""
CAN/ISOTP stuff
"""


class CANHackerMsg(TypedDict):
    timestamp: float
    can_id: int
    payload: list[int]

# includes assembled ISO-TP messages
class ProcessedMsg(TypedDict):
    timestamp: float # of first frame
    can_id: int
    payload: list[int]

class BufferedProcessedMsg(TypedDict):
    timestamp: float # of first frame
    last_seq: int
    length: int
    payload: list[int]

class ISOTP_FrameType(int, Enum):
    SINGLE_FRAME = 0x0
    FIRST_FRAME = 0x1
    CONSECUTIVE_FRAME = 0x2
    FLOW_CONTROL = 0x3


# parse trace file into raw can messages
def parse_canhacker_trc(file_path: str) -> Iterator[CANHackerMsg]:
    with open(file_path, "r") as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith("Time"): # skip header or some bs
                continue
            
            # assumes that timestamp is present
            split = line.split()

            yield CANHackerMsg({
                "timestamp": float(split[0]),
                "can_id": int(split[1], 16),
                "payload": [int(byte, 16) for byte in split[3:]] # skips data length byte
            })


#  handle iso-tp messages and emit assembled messages stripped of iso-tp headers
def parse_isotp_messages(raw_messages: Iterator[CANHackerMsg]) -> Iterator[ProcessedMsg]:
    buffer: Dict[int, BufferedProcessedMsg] = {}

    for raw_message in raw_messages:
        pci_byte = raw_message["payload"][0]
        pci_high = (pci_byte & 0xF0) >> 4 # frame type
        pci_low = pci_byte & 0x0F # frame specific data

        match pci_high:
            case ISOTP_FrameType.SINGLE_FRAME:
                yield ProcessedMsg({
                    "timestamp": raw_message["timestamp"],
                    "can_id": raw_message["can_id"],
                    "payload": raw_message["payload"][1:pci_low + 1],
                })
            case ISOTP_FrameType.FIRST_FRAME:
                upper_len_bits = pci_low 
                lower_len_bits = raw_message["payload"][1]
                data_length = (upper_len_bits << 8) | lower_len_bits

                buffer[raw_message["can_id"]] = BufferedProcessedMsg({
                    "timestamp": raw_message["timestamp"],  
                    "last_seq": 0,
                    "length": data_length,
                    "payload": raw_message["payload"][2:],
                })
            case ISOTP_FrameType.CONSECUTIVE_FRAME:
                if raw_message["can_id"] not in buffer:
                    continue # trc file likely started with incomplete data

                sequence_num = pci_low
                # TODO: check sequence number, but probably not an issue

                buffered_msg = buffer[raw_message["can_id"]]
                buffered_msg["payload"].extend(raw_message["payload"][1:])

                if len(buffered_msg["payload"]) >= buffered_msg["length"]:
                    yield ProcessedMsg({
                        "timestamp": buffered_msg["timestamp"],
                        "can_id": raw_message["can_id"],
                        "payload": buffered_msg["payload"][:buffered_msg["length"]], # trim any padding
                    })
                    del buffer[raw_message["can_id"]]
            case ISOTP_FrameType.FLOW_CONTROL: # flow control frame
                # don't care
                pass
            case _:
                print("wthelly")
                continue


"""
SSM stuff
"""

# - requests

# first byte - service identifier
class SSMServiceID(int, Enum):
    REQ_READ_MEMORY_BY_ADDR_LIST = 0xA8
    RES_READ_MEMORY_BY_ADDR_LIST = 0xE8
    REQ_IDK_SOMETHING_WITH_ABS_VBC = 0x22 
    RES_IDK_SOMETHING_WITH_ABS_VBC = 0x62

# second byte - subfunction
class SSMSubfunction(int, Enum):
    PLAIN_LIST = 0x00
    START_CONTINUOUS_READ = 0x01
    STOP_CONTINUOUS_READ = 0x02
    IDK_SOMETHING_WITH_ABS_VDC = 0x10

class SSMRequest(TypedDict):
    timestamp: float
    service: SSMServiceID
    subfunction: SSMSubfunction
    payload: list[int] # list of memory addresses (24-bit combined)

class SSMResponse(TypedDict):
    timestamp: float
    service: SSMServiceID
    payload: list[int] # list of bytes read from memory

class SSMMessage(TypedDict):
    timestamp: float
    sid: int
    command: SSMServiceID
    subfunction: SSMSubfunction
    payload: list[int]

class RawAddrValue(TypedDict):
    address: int
    raw_value: int

class ProcessedECUAddr(TypedDict):
    addresses: list[int]
    value: int
    # optional fields, empty if address is unknown
    name: str
    unit: str

# - functions

def ssm_parse_list_request(message: ProcessedMsg) -> SSMRequest:
    processed_payload = []

    # ssm uses 24 bit addresses, skip first two bytes (service id, subfunction)
    num_requested_addrs = (len(message["payload"]) - 2) // 3
    for i in range(num_requested_addrs):
        addr_start = 2 + (i * 3)
        addr_bytes = message["payload"][addr_start:addr_start + 3]
        memory_addr = (addr_bytes[0] << 16) | (addr_bytes[1] << 8) | addr_bytes[2]
        processed_payload.append(memory_addr)

    return SSMRequest({
        "timestamp": message["timestamp"],
        "service": SSMServiceID(message["payload"][0]),
        "subfunction": SSMSubfunction(message["payload"][1]),
        "payload": processed_payload,
    })

def ssm_parse_list_response(message: ProcessedMsg) -> SSMResponse:
    return SSMResponse({
        "timestamp": message["timestamp"],
        "service": SSMServiceID(message["payload"][0]),
        "payload": message["payload"][1:],
    })

# TODO: just move this into a class method or something to make it cleaner
# returns list of addresses with their values from a response and its corresponding request
def process_ssm_message(address_lookup: Dict[int, AddressInfo], message: ProcessedMsg) -> Optional[list[ProcessedECUAddr]]:
    # jank static variable
    if not hasattr(process_ssm_message, "last_ssm_request"):
        process_ssm_message.last_ssm_request = None # pyright: ignore[reportFunctionMemberAccess] (shut up let me do bad things)

    # print(f"ID: {hex(msg['can_id'])}\nDATA: {[hex(b) for b in msg['payload']]}")
        
    service_id = message["payload"][0]
    match service_id:
        case SSMServiceID.REQ_READ_MEMORY_BY_ADDR_LIST:
            ssm_request = ssm_parse_list_request(message)
            # print(f"SSM requested addresses: {[f'{i:#08x}' for i in ssm_request['payload']]}")
            
            process_ssm_message.last_ssm_request = ssm_request # pyright: ignore[reportFunctionMemberAccess]
        case SSMServiceID.RES_READ_MEMORY_BY_ADDR_LIST:
            if process_ssm_message.last_ssm_request is None: # pyright: ignore[reportFunctionMemberAccess]
                return None

            ssm_response = ssm_parse_list_response(message)
            # print(f"SSM address responses: {[f'{i:#04x}' for i in ssm_response['payload']]}")

            # check if matches last request, at least in terms of requested number of addresses

            num_requested = len(process_ssm_message.last_ssm_request["payload"]) # pyright: ignore[reportFunctionMemberAccess]
            num_received = len(ssm_response["payload"])
            if num_received != num_requested:
                print(f"aw hell nah: received {num_received}, does not match requested ({num_requested})")
                return None

            # emit single or combined address + value pairs

            addr_values: list[ProcessedECUAddr] = []
            i = 0
            while i < num_requested:
                addr = process_ssm_message.last_ssm_request["payload"][i] # pyright: ignore[reportFunctionMemberAccess]

                # check if memory address is known and if the value spans multiple addresses
                if addr in address_lookup:
                    ref = address_lookup[addr]

                    addresses = []
                    combined_value = 0
                    j = 0
                    while j < ref["length"]:
                        addresses.append(addr + j)
                        raw_value = ssm_response["payload"][i + j]
                        combined_value |= raw_value << (8 * (ref["length"] - j - 1))
                        j += 1
                    
                    final_value = eval(ref["value"]["expr"], globals=None, locals={"x": combined_value})

                    addr_values.append(ProcessedECUAddr({
                        "addresses": addresses,
                        "value": final_value,
                        "name": ref["name"],
                        "unit": ref["value"]["unit"],
                    }))

                    i += ref["length"] 
                else:
                    # assume single address / byte value
                    addr_values.append(ProcessedECUAddr({
                        "addresses": [addr],
                        "value": ssm_response["payload"][i],
                        "name": "",
                        "unit": "",
                    }))

                    i += 1
            return addr_values
        case _:
            print("what the absolute shidd")
            return None


"""
ISO 14229-1 UDS stuff
"""


class UDSServiceID(int, Enum):
   READ_DATA_BY_ID = 0x22
   POSITIVE_RESPONSE = 0x62

class UDSRequest(TypedDict):
    timestamp: float
    requested_id: int # 16 bit id

class UDSResponse(TypedDict):
    timestamp: float
    requested_id: int # 16 bit id
    payload: int # 16 bit value

class ProcessedUDSServiceID(TypedDict):
    timestamp: float
    requested_id: int
    value: int
    # optional fields, empty if id is unknown
    name: str
    unit: str


# returns parsed uds responses, ignores requests
def process_uds_message(address_lookup: Dict[int, AddressInfo], message: ProcessedMsg) -> Optional[ProcessedUDSServiceID]:
    service_id = message["payload"][0]
    match service_id:
        case UDSServiceID.READ_DATA_BY_ID:
            uds_request = UDSRequest({
                "timestamp": message["timestamp"],
                "requested_id": (message["payload"][1] << 8) | message["payload"][2]
            })
            # TODO: might not even really need this

            return None
        case UDSServiceID.POSITIVE_RESPONSE:
            uds_response = UDSResponse({
                "timestamp": message["timestamp"],
                "requested_id": (message["payload"][1] << 8) | message["payload"][2],
                "payload": (message["payload"][3] << 8) | message["payload"][4],
            })

            addr = uds_response["requested_id"]
            if addr in address_lookup:
                ref = address_lookup[addr]
                final_value = eval(ref["value"]["expr"], globals=None, locals={"x": uds_response["payload"]})

                return ProcessedUDSServiceID({
                    "timestamp": uds_response["timestamp"],     
                    "requested_id": uds_response["requested_id"],
                    "value": final_value,
                    "name": ref["name"],
                    "unit": ref["value"]["unit"],
                })
            else:
                return ProcessedUDSServiceID({
                    "timestamp": uds_response["timestamp"],     
                    "requested_id": uds_response["requested_id"],
                    "value": uds_response["payload"],
                    "name": "",
                    "unit": "",
                })
        case _:
            print(f"unknown service id {hex(service_id)}")
            return None


def process_messages_to_dataframe(isotp_messages: list[ProcessedMsg]):
    df = pd.DataFrame()

    for message in isotp_messages:
        # print(f"timestamp: {message['timestamp']}, can_id: {hex(message['can_id'])}, payload: {[hex(b) for b in message['payload']]}")

        id = message["can_id"]
        match id:
            case ModuleID.ECU_REQ | ModuleID.ECU_RES:
                ecu_response = process_ssm_message(address_lookup["ecu"], message)
                if ecu_response:
                    fields = dict(
                        map(
                            lambda x: (
                                x["name"] 
                                    if x["name"] 
                                    else "".join(f"{addr:#08x}" for addr in x["addresses"]),
                                x["value"] if x["name"] else hex(int(x["value"]))
                            ), 
                            ecu_response    
                        )
                    )
                    frame = {"timestamp": message["timestamp"], **fields}
                    df = pd.concat([df, pd.DataFrame([frame])], ignore_index=True)
                # print(ecu_response)
            case ModuleID.ABS_VDC_REQ | ModuleID.ABS_VDC_RES:
                abs_vcs_response = process_uds_message(address_lookup["abs_vdc"], message)
                if abs_vcs_response:
                    fields = {
                        abs_vcs_response["name"] if abs_vcs_response["name"] else hex(abs_vcs_response["requested_id"]): 
                        abs_vcs_response["value"] if abs_vcs_response["name"] else hex(abs_vcs_response["value"])
                    }
                    frame = {"timestamp": message["timestamp"], **fields}
                    df = pd.concat([df, pd.DataFrame([frame])], ignore_index=True)
                # print(abs_vcs_response)
            case _:
                print(f"skipping message with id {hex(id)}")
                continue

    return df


"""
main
"""


if __name__ == "__main__":
    parser = argparse.ArgumentParser(
        description="lmao"
    )
    parser.add_argument(
        "-t",
        "--trc",
        dest="trc",
        metavar="PATH",
        type=str,
        required=False,
        help="Path to the CANHacker .trc file to parse",
    )
    parser.add_argument(
        "-o",
        "--output",
        dest="output",
        type=str,
        choices=["json", "csv"],
        default="json",
        required=False,
        help="Whether or not to display in slop format or csv format",
    )
    args = parser.parse_args()

    address_lookup = init_reference()

    # for being lazy
    # trc_path = args.trc if args.trc is not None else "/Users/nic/Downloads/staticreadings/dam-single.trc"
    trc_path = args.trc if args.trc is not None else "C:\\Users\\Nic\\Downloads\\static-2\\steering-angle-center-to-left-to-right-to-center.trc"

    raw_can_messages = parse_canhacker_trc(trc_path)
    isotp_messages = parse_isotp_messages(raw_can_messages)

    df = process_messages_to_dataframe(list(isotp_messages))

    match args.output:
        case "json":
            print(df.to_json(orient="records", lines=True))
            pass
        case "csv":
            print(df.to_csv(index=False))
            pass
