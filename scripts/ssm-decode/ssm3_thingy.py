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


class CANHackerMsg(TypedDict):
    timestamp: float
    can_id: int
    data_bytes: list[int]

# includes assembled ISO-TP messages
class ProcessedMsg(TypedDict):
    timestamp: float # of first frame
    can_id: int
    data_bytes: list[int]

class BufferedProcessedMsg(TypedDict):
    timestamp: float # of first frame
    last_seq: int
    length: int
    data_bytes: list[int]



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
                "data_bytes": [int(byte, 16) for byte in split[3:]]
            })


def process_isotp_messages(raw_messages: Iterator[CANHackerMsg]) -> Iterator[ProcessedMsg]:
    buffer: Dict[int, BufferedProcessedMsg] = {}

    for raw_message in raw_messages:
        pci_byte = raw_message["data_bytes"][0]
        pci_high = (pci_byte & 0xF0) >> 4
        pci_low = pci_byte & 0x0F

        match pci_high:
            case 0x0: # single frame
                yield ProcessedMsg({
                    "timestamp": raw_message["timestamp"],
                    "can_id": raw_message["can_id"],
                    "data_bytes": raw_message["data_bytes"][1:],
                })
            case 0x1: # first frame
                # pci_low = upper 4 bits of total length, length_byte = lower 8 bits of total length
                upper_len_bits = pci_low 
                lower_len_bits = raw_message["data_bytes"][1]
                msg_length = (upper_len_bits << 8) | lower_len_bits

                buffer[raw_message["can_id"]] = BufferedProcessedMsg({
                    "timestamp": raw_message["timestamp"],  
                    "last_seq": 0,
                    "length": msg_length,
                    "data_bytes": raw_message["data_bytes"][2:],
                })
            case 0x2: # consecutive frame
                sequence_num = pci_low
                # TODO: check sequence number
                # TODO: check if id is even in the buffer

                buffered_msg = buffer[raw_message["can_id"]]
                buffered_msg["data_bytes"].extend(raw_message["data_bytes"][1:])

                if len(buffered_msg["data_bytes"]) >= buffered_msg["length"]:
                    yield ProcessedMsg({
                        "timestamp": buffered_msg["timestamp"],
                        "can_id": raw_message["can_id"],
                        "data_bytes": buffered_msg["data_bytes"][:buffered_msg["length"]], # trim any padding
                    })
                    del buffer[raw_message["can_id"]]
            case 0x3: # flow control frame
                # don't care
                pass
            case _:
                print("wthelly")
                continue


if __name__ == "__main__":
    raw_can_messages = parse_canhacker_trc("C:\\Users\\Nic\\Downloads\\staticreadings\\ethanolconcfinal-single.trc")
    assembled_isotp_messages = process_isotp_messages(raw_can_messages)

    for msg in assembled_isotp_messages:
        print(f"ID: {hex(msg['can_id'])}\nDATA: {[hex(b) for b in msg['data_bytes']]}\n\n")