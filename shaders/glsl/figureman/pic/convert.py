from PIL import Image

def convert_rgb_to_rgba(input_path, output_path):
    # 打开原始RGB图像
    img = Image.open(input_path).convert("RGBA")

    # 创建一个与图像相同大小的完全不透明的alpha通道
    alpha_channel = Image.new('L', img.size, 255)  # 255表示完全不透明

    # 添加alpha通道到图像
    new_img = Image.merge("RGBA", img.split()[:3] + (alpha_channel,))

    # 保存结果图像
    new_img.save(output_path, "PNG")

# 示例调用
convert_rgb_to_rgba("D:\\CodeRepo\\vulkanSamplers\\Vulkan\\shaders\\glsl\\figureman\\pic\\T_EYE_NORMALS.png", "D:\\CodeRepo\\vulkanSamplers\\Vulkan\\shaders\\glsl\\figureman\\pic\\T_EYE_NORMALS_RGBA.png")