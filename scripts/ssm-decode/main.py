import yaml

from enum import Enum
from typing import Dict, Iterator, TypedDict, cast


""""
0x7E0 request to ecu
0x7E8 response from ecu

unconfirmed:
0x7E1	request to TCU
0x7E9	response from TCU
0x7E2	request to ABS/VDC
0x7EA	response from ABS/VDC
"""


"""
memory addr stuff
"""


class AddressValue(TypedDict):
    unit: str
    expr: str
class AddressInfo(TypedDict):
    name: str
    value: AddressValue


def print_addresses(latest_parameters: Dict[int, int]) -> None:
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
                    "payload": raw_message["payload"][1:],
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
                    continue # trc file likely has response first without corresponding request

                sequence_num = pci_low
                # TODO: check sequence number

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


"""
main
"""


if __name__ == "__main__":
    with open("known-addresses.yaml", "r") as f:
        address_lookup = cast(Dict[int, AddressInfo], yaml.safe_load(f))
        print(address_lookup)

    # raw_can_messages = parse_canhacker_trc("C:\\Users\\Nic\\Downloads\\staticreadings\\ethanolconcfinal-single.trc")
    raw_can_messages = parse_canhacker_trc("/Users/nic/Downloads/staticreadings/dam-single.trc")
    assembled_isotp_messages = process_isotp_messages(raw_can_messages)

    latest_parameters: Dict[int, int] = {}
    last_ssm_request: SSMRequest | None = None

    for msg in assembled_isotp_messages:
        # print(f"ID: {hex(msg['can_id'])}\nDATA: {[hex(b) for b in msg['payload']]}")

        service_id = msg["payload"][0]
        match service_id:
            case SSMServiceID.REQ_READ_MEMORY_BY_ADDR_LIST:
                ssm_request = ssm_parse_list_request(msg)
                # print(f"SSM requested addresses: {[f'{i:#08x}' for i in ssm_request['payload']]}")
                
                last_ssm_request = ssm_request
            case SSMServiceID.RES_READ_MEMORY_BY_ADDR_LIST:
                if last_ssm_request is None:
                    continue

                ssm_response = ssm_parse_list_response(msg)
                # print(f"SSM address responses: {[f'{i:#04x}' for i in ssm_response['payload']]}")

                # check if matches last request, at least in terms of requested number of addresses

                num_requested = len(last_ssm_request["payload"])
                num_received = len(ssm_response["payload"])

                if num_received != num_requested:
                    print(f"aw hell nah: received {num_received}, does not match requested ({num_requested})")
                    continue

                # upsert memory addresses with their latest value

                for i, addr in enumerate(last_ssm_request["payload"]):
                    latest_parameters[addr] = ssm_response["payload"][i]
            case _:
                print("what the absolute shidd")

        # print()

    print("latest parameters:")
    print_addresses(latest_parameters) 

