import sys

def process_html(input_file, output_file):
    with open(input_file, 'r') as f:
        content = f.read()
    
    if input_file[-4:] == 'html':
        content = content.replace('\n', '').replace('\r', '').replace('"', '\\"')
        c_string = 'static const char index_html[] = "' + content + '";'

    if input_file[-3:] == 'css':
        content = content.replace('\n', ' \\\n').replace('\r', '')
        c_string = 'static const char weather_css[] = "' + content + '";'
        
    with open(output_file, 'w') as f:
        f.write(c_string)

if __name__ == "__main__":
    process_html(sys.argv[1], sys.argv[2])