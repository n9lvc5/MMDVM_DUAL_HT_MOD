with open('RS129.cpp', 'r') as f:
    content = f.read()

import re
table_match = re.search(r'const uint8_t EXP_TABLE\[512\] = \{(.*?)\};', content, re.DOTALL)
if table_match:
    table_str = table_match.group(1)
    # The table currently has 511 entries. We need to add one more to make it 512.
    # The missing entry is the last one in the repeating sequence.
    # If the sequence starts at exp[0]=1 and repeats every 255 entries, 
    # then exp[511] should be the same as exp[511-255] = exp[256].
    # Wait, the table in memory trace had 512 entries.
    # After my fix it has 511.
    new_table_str = table_str.strip()
    if not new_table_str.endswith(','):
        new_table_str += ','
    new_table_str += ' 0x02U' # exp[511] = exp[256] = exp[1] = 2
    
    new_content = content.replace(table_match.group(1), new_table_str)
    with open('RS129.cpp', 'w') as f:
        f.write(new_content)
