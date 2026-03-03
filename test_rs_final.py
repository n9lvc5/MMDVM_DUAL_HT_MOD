import re
with open('RS129.cpp', 'r') as f:
    content = f.read()
    table_match = re.search(r'const uint8_t EXP_TABLE\[512\] = \{(.*?)\};', content, re.DOTALL)
    if table_match:
        table_str = table_match.group(1)
        cpp_table = [int(x.strip().replace('U', ''), 16) for x in table_str.split(',') if x.strip()]
        print(f"Table length: {len(cpp_table)}")
        if len(cpp_table) == 512:
            print("SUCCESS: Table length is 512")
        else:
            print("FAILURE: Table length is not 512")
