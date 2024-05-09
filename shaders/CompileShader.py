import os
import sys
import subprocess

def main():
    print('shader type:', sys.argv[1])
    print('shader name:', sys.argv[2])
    curDir = os.getcwd()
    print("Current directory:" + curDir)
    glslFilePath = curDir + "\\glsl\\" + sys.argv[2]
    hlslFilePath = curDir + "\\hlsl\\" + sys.argv[2]
    print("GlslFilePath:" + glslFilePath)
    print("HlslFilePath:" + hlslFilePath)
    dxcPath = "C:\\VulkanSDK\\1.3.280.0\\Bin\\dxc.exe"
    glslangPath = "C:\\VulkanSDK\\1.3.280.0\\Bin\\glslang.exe"
    if(os.path.isfile(glslFilePath)):
        print("Exist glslFilePath")
        args = ['-V', glslFilePath, '-o', glslFilePath+".spv"]
        process=subprocess.run([glslangPath]+args,stdout=subprocess.PIPE,stderr=subprocess.PIPE)
        print("GLSLOutput:",process.stdout.decode('utf-8'))
        print("GLSLErrors:",process.stderr.decode('utf-8'))
        #os.system(glslangPath + " -V "+glslFilePath+" -o "+ glslFilePath+".spv")
    if(os.path.isfile(hlslFilePath)):
        print("Exist hlslFilePath")
        if(sys.argv[1]=="vert"):
            args = ['-spirv', '-T','vs_6_0', '-E', 'main', hlslFilePath,'-Fo', hlslFilePath+".spv"]
            process=subprocess.run([dxcPath]+args,stdout=subprocess.PIPE,stderr=subprocess.PIPE)
            print("HLSLOutput:",process.stdout.decode('utf-8'))
            print("HLSLErrors:",process.stderr.decode('utf-8'))
            #os.system(dxcPath + " -spirv -T vs_6_0 -E main "+hlslFilePath+" -Fo "+ hlslFilePath+".spv")
        else:
            args = ['-spirv', '-T','ps_6_0', '-E', 'main', hlslFilePath,'-Fo', hlslFilePath+".spv"]
            process=subprocess.run([dxcPath]+args,stdout=subprocess.PIPE,stderr=subprocess.PIPE)
            print("HLSLOutput:",process.stdout.decode('utf-8'))
            print("HLSLErrors:",process.stderr.decode('utf-8'))
            #os.system(dxcPath + " -spirv -T ps_6_0 -E main "+hlslFilePath+" -Fo "+ hlslFilePath+".spv")

if __name__ == "__main__":
    main()


