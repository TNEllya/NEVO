from PIL import Image, ImageFilter

src = r'C:\Users\yzd20\Pictures\clear ne.png'
dst = r'C:\Users\yzd20\Desktop\Project\NEVO\webclient\electron\app-icon.ico'

img = Image.open(src)
if img.mode != 'RGBA':
    img = img.convert('RGBA')

# Windows 常用图标尺寸，覆盖任务栏/开始菜单/桌面/安装向导
sizes = [16, 20, 24, 32, 40, 48, 64, 96, 128, 192, 256]
imgs = []

for size in sizes:
    # 大图用 LANCZOS，小图用 BICUBIC 并加轻微锐化避免模糊
    if size >= 64:
        resized = img.resize((size, size), Image.LANCZOS)
    else:
        resized = img.resize((size, size), Image.BICUBIC)
        # 对小尺寸应用轻微锐化，提升边缘清晰度
        resized = resized.filter(ImageFilter.SHARPEN)
    imgs.append(resized)

imgs[0].save(dst, format='ICO', sizes=[(i.width, i.height) for i in imgs])
print('Created', dst, 'with sizes', sizes)
