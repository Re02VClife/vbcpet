from PIL import Image
import serial, time

# 1. make a solid red 240x320 jpeg
img = Image.new('RGB', (240, 320), (255, 0, 0))
img.save(r'D:\vscode\vbcpet\_red_test.jpg', 'JPEG', quality=80)
jpeg = open(r'D:\vscode\vbcpet\_red_test.jpg', 'rb').read()
print('red test jpeg:', len(jpeg), 'bytes')

# 2. send it 5x over the demux protocol
ser = serial.Serial('COM8', 2000000, timeout=1, write_timeout=5)
header = ('FRAME:%08x\n' % len(jpeg)).encode()
for i in range(5):
    ser.write(header)
    ser.write(jpeg)
    ser.flush()
    print('sent red frame', i + 1)
    time.sleep(1)
ser.close()
print('DONE - watch top-left quarter of the screen')
