from enum import Enum
from typing import Dict, Iterator, TypedDict


""""
0x7E0 request to ecu
0x7E8 response from ecu

unconfirmed:
0x7E1	request to TCU
0x7E9	response from TCU
0x7E2	request to ABS/VDC
0x7EA	response from ABS/VDC
"""


# --- CAN stuff


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
        pci_high = (pci_byte & 0xF0) >> 4
        pci_low = pci_byte & 0x0F

        match pci_high:
            case 0x0: # single frame
                yield ProcessedMsg({
                    "timestamp": raw_message["timestamp"],
                    "can_id": raw_message["can_id"],
                    "payload": raw_message["payload"][1:],
                })
            case 0x1: # first frame
                upper_len_bits = pci_low 
                lower_len_bits = raw_message["payload"][1]
                data_length = (upper_len_bits << 8) | lower_len_bits

                buffer[raw_message["can_id"]] = BufferedProcessedMsg({
                    "timestamp": raw_message["timestamp"],  
                    "last_seq": 0,
                    "length": data_length,
                    "payload": raw_message["payload"][2:],
                })
            case 0x2: # consecutive frame
                sequence_num = pci_low
                # TODO: check sequence number
                # TODO: check if id is even in the buffer

                buffered_msg = buffer[raw_message["can_id"]]
                buffered_msg["payload"].extend(raw_message["payload"][1:])

                if len(buffered_msg["payload"]) >= buffered_msg["length"]:
                    yield ProcessedMsg({
                        "timestamp": buffered_msg["timestamp"],
                        "can_id": raw_message["can_id"],
                        "payload": buffered_msg["payload"][:buffered_msg["length"]], # trim any padding
                    })
                    del buffer[raw_message["can_id"]]
            case 0x3: # flow control frame
                # don't care
                pass
            case _:
                print("wthelly")
                continue


# --- SSM3 stuff

# - requests

# first byte - service identifier
class SSM3ServiceID(int, Enum):
    REQ_READ_MEMORY_BY_ADDR_LIST = 0xA8
    RES_READ_MEMORY_BY_ADDR_LIST = 0xE8
    REQ_READ_SINGLE_PARAMETER = 0xA4
    RES_READ_SINGLE_PARAMETER = 0xE4

# second byte - subfunction
class SSM3Subfunction(int, Enum):
    PLAIN_LIST = 0x00
    START_CONTINUOUS_READ = 0x01
    STOP_CONTINUOUS_READ = 0x02

class SSM3Request(TypedDict):
    timestamp: float
    service: SSM3ServiceID
    subfunction: SSM3Subfunction
    payload: list[int] # list of memory addresses (24-bit combined)

class SSM3Response(TypedDict):
    timestamp: float
    service: SSM3ServiceID
    payload: list[int] # list of bytes read from memory

class SSM3Message(TypedDict):
    timestamp: float
    sid: int
    command: SSM3ServiceID
    subfunction: SSM3Subfunction
    payload: list[int]

# - functions

def ssm3_parse_list_request(message: ProcessedMsg) -> SSM3Request:
    processed_payload = []

    num_requested_addrs = len(message["payload"]) - 2
    for i in range(num_requested_addrs // 3):
        addr_start = 2 + (i * 3)
        addr_bytes = message["payload"][addr_start:addr_start + 3]
        memory_addr = (addr_bytes[0] << 16) | (addr_bytes[1] << 8) | addr_bytes[2]
        processed_payload.append(memory_addr)

    return SSM3Request({
        "timestamp": message["timestamp"],
        "service": SSM3ServiceID(message["payload"][0]),
        "subfunction": SSM3Subfunction(message["payload"][1]),
        "payload": processed_payload,
    })

def ssm3_parse_list_response(message: ProcessedMsg) -> SSM3Response:
    return SSM3Request({
        "timestamp": message["timestamp"],
        "service": SSM3ServiceID(message["payload"][0]),
        "payload": message["payload"][1:],
    })


# --- main


if __name__ == "__main__":
    # raw_can_messages = parse_canhacker_trc("C:\\Users\\Nic\\Downloads\\staticreadings\\ethanolconcfinal-single.trc")
    raw_can_messages = parse_canhacker_trc("/Users/nic/Downloads/staticreadings/ethanolconcfinal-single.trc")
    assembled_isotp_messages = process_isotp_messages(raw_can_messages)

    latest_parameters: Dict[int, int] = {}
    last_ssm3_request: SSM3Request | None = None

    for msg in assembled_isotp_messages:
        print(f"ID: {hex(msg['can_id'])}\nDATA: {[hex(b) for b in msg['payload']]}")

        service_id = msg["payload"][0]

        match service_id:
            case SSM3ServiceID.REQ_READ_MEMORY_BY_ADDR_LIST:
                ssm3_request = ssm3_parse_list_request(msg)
                print(f"SSM3 requested addresses: {[f'0x{i:03x}' for i in ssm3_request['payload']]}")
                
                last_ssm3_request = ssm3_request
            case SSM3ServiceID.RES_READ_MEMORY_BY_ADDR_LIST:
                if last_ssm3_request is None:
                    continue

                ssm3_response = ssm3_parse_list_response(msg)
                print(f"SSM3 address responses: {[f'0x{i:02x}' for i in ssm3_response['payload']]}")

                # check if matches last request, at least in terms of requested number of addresses

                num_requested = len(last_ssm3_request["payload"])
                num_received = len(ssm3_response["payload"])

                if num_received != num_requested:
                    print(f"aw hell nah: received {num_received}, does not match requested ({num_requested})")
                    continue

                # upsert memory addresses with their latest value

                for i, addr in enumerate(last_ssm3_request["payload"]):
                    latest_parameters[addr] = ssm3_response["payload"][i]
            case _:
                print("what the absolute shidd")

        print("latest parameters:")
        for addr, value in latest_parameters.items():
            print(f"\t{addr:#08x}: {value:#04x}")    

        print()


        