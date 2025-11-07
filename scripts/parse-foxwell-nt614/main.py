import json


# quick and dirty script to just get the values out into a list you can copy and paste into a spreadsheet


if __name__ == "__main__":
    file_path = "C:\\Users\\Nic\\Downloads\\SUBARUbrk"
    with open(file_path, "r") as f:
        file_contents = f.readlines()

    # strip header
    # header_line = file_contents[0]
    # break_idx = header_line.index("¦")
    # file_contents[0] = header_line[break_idx + 1:]
    
    # just ignore the first line completely because sometimes it contains data at the end but haven't noticed a consistent pattern yet
    file_contents = file_contents[1:]

    # strip extra padding and whatever other weird characters
    file_contents = [line.strip() for line in file_contents]
    for i in range(len(file_contents)):
        line = file_contents[i]
        start = line.index("[")
        file_contents[i] = line[start:]

    # convert to numerical values
    parsed_data = []
    for line in file_contents:
        parsed = list(json.loads(line))
        parsed_data.append(parsed[0])

    for value in parsed_data:
        print(value)