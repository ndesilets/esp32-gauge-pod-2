import argparse
import yaml

from enum import Enum
from pathlib import Path
from typing import Dict, Iterator, TypedDict, cast


"""
notes:
    0x7E0   request to ecu
    0x7E8   response from ecu
    0x7B0	request to ABS/VDC
    0x7B8	response from ABS/VDC
"""


"""
memory addr stuff
"""


class AddressValue(TypedDict):
    unit: str
    expr: str

class AddressInfo(TypedDict):
    name: str
    length: int
    value: AddressValue

def pretty_print_address(latest_parameters: Dict[int, int]) -> None:
    for addr, value in sorted(latest_parameters.items()):
        if addr in address_lookup:
            addr_info = address_lookup[addr]
            name = addr_info["name"]
            unit = addr_info["value"]["unit"]
            expr = addr_info["value"]["expr"]

            x = value
            transformed_value = eval(expr)

            print(f"{addr:#08x} ({name}): {transformed_value} {unit}")
        else:
            print(f"{addr:#08x}: {value:#04x}")


"""
CAN stuff
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
def process_isotp_messages(raw_messages: Iterator[CANHackerMsg]) -> Iterator[ProcessedMsg]:
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
    REQ_READ_SINGLE_PARAMETER = 0xA4
    RES_READ_SINGLE_PARAMETER = 0xE4

# second byte - subfunction
class SSMSubfunction(int, Enum):
    PLAIN_LIST = 0x00
    START_CONTINUOUS_READ = 0x01
    STOP_CONTINUOUS_READ = 0x02

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

class ProcessedAddrValue(TypedDict):
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
# produce list of addresses with their values from request and response pair
def process_ssm_messages(address_lookup: Dict[int, AddressInfo], messages: Iterator[ProcessedMsg]) -> Iterator[list[ProcessedAddrValue]]:
    # jank static variable
    if not hasattr(process_ssm_messages, "last_ssm_request"):
        process_ssm_messages.last_ssm_request = None # pyright: ignore[reportFunctionMemberAccess] (shut up let me do bad things)

    # print(f"ID: {hex(msg['can_id'])}\nDATA: {[hex(b) for b in msg['payload']]}")

    for message in messages:
        service_id = message["payload"][0]
        match service_id:
            case SSMServiceID.REQ_READ_MEMORY_BY_ADDR_LIST:
                ssm_request = ssm_parse_list_request(message)
                # print(f"SSM requested addresses: {[f'{i:#08x}' for i in ssm_request['payload']]}")
                
                process_ssm_messages.last_ssm_request = ssm_request # pyright: ignore[reportFunctionMemberAccess]
            case SSMServiceID.RES_READ_MEMORY_BY_ADDR_LIST:
                if process_ssm_messages.last_ssm_request is None: # pyright: ignore[reportFunctionMemberAccess]
                    continue

                ssm_response = ssm_parse_list_response(message)
                # print(f"SSM address responses: {[f'{i:#04x}' for i in ssm_response['payload']]}")

                # check if matches last request, at least in terms of requested number of addresses

                num_requested = len(process_ssm_messages.last_ssm_request["payload"]) # pyright: ignore[reportFunctionMemberAccess]
                num_received = len(ssm_response["payload"])
                if num_received != num_requested:
                    print(f"aw hell nah: received {num_received}, does not match requested ({num_requested})")
                    continue

                # emit single or combined address + value pairs

                addr_values: list[ProcessedAddrValue] = []
                i = 0
                while i < num_requested:
                    addr = process_ssm_messages.last_ssm_request["payload"][i] # pyright: ignore[reportFunctionMemberAccess]

                    # check if memory address is known and if the value spans multiple addresses
                    if addr in address_lookup:
                        # handle single/multiple address+byte values

                        ref = address_lookup[addr]
                        addr_length = ref["length"] 

                        addresses = []
                        combined_value = 0
                        j = 0
                        while j < addr_length:
                            addresses.append(addr + j)
                            raw_value = ssm_response["payload"][i + j]
                            combined_value |= raw_value << (8 * (addr_length - j - 1))
                            j += 1
                        
                        # apply expression to combined value
                        x = combined_value # x is referenced in expr
                        transformed_value = eval(ref["value"]["expr"])

                        addr_values.append(ProcessedAddrValue({
                            "addresses": addresses,
                            "value": transformed_value,
                            "name": ref["name"],
                            "unit": ref["value"]["unit"],
                        }))

                        i += addr_length
                    else:
                        # assume single address / byte value
                        addr_values.append(ProcessedAddrValue({
                            "addresses": [addr],
                            "value": ssm_response["payload"][i],
                            "name": "",
                            "unit": "",
                        }))

                        i += 1
                yield addr_values
            case _:
                print("what the absolute shidd")
                pass


"""
output stuff
"""


def display_slop(message_frames: list[list[ProcessedAddrValue]]):
    for frame in message_frames:
        for message in sorted(frame, key=lambda x: x["addresses"][0]):
            known_address = message["name"] != "" and message["unit"] != ""
            if known_address:
                print(f"{message['addresses'][0]:#08x} ({message['name']}): {message['value']} {message['unit']}")
            else:
                print(f"{message['addresses'][0]:#08x}: {message['value']:#04x}")
        print()

def display_csv(message_frames: list[list[ProcessedAddrValue]]):
    pass


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
        choices=["slop", "csv"],
        default="slop",
        required=False,
        help="Whether or not to display in slop format or csv format",
    )
    args = parser.parse_args()

    with open("known-addresses.yaml", "r") as f:
        address_lookup = cast(Dict[int, AddressInfo], yaml.safe_load(f))
        # print(address_lookup)

    # for being lazy
    trc_path = args.trc if args.trc is not None else "/Users/nic/Downloads/staticreadings/boost.trc"

    raw_can_messages = parse_canhacker_trc(trc_path)
    assembled_isotp_messages = process_isotp_messages(raw_can_messages)
    processed_ssm_frames = process_ssm_messages(address_lookup, assembled_isotp_messages)

    match args.output:
        case "slop":
            display_slop(list(processed_ssm_frames))
        case "csv":
            display_csv(list(processed_ssm_frames))
            pass
    