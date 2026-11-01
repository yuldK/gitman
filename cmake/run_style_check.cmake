if(NOT DEFINED ROOT_DIRECTORY)
    message(FATAL_ERROR "ROOT_DIRECTORY is required.")
endif()

# PowerShell은 **돌 때마다** PATH에서 찾는다.
# 구성 시점에 찾은 절대 경로를 넘기면 그 경로가 build 디렉터리에 굳는다. 같은 PowerShell이 설치 방식
# (설치 관리자·Microsoft Store·winget)이나 판 올림으로 자리를 옮기면 낡은 경로를 불러 검사가 늘 실패했다.
find_program(powershell_executable NAMES pwsh powershell NO_CACHE)
if(NOT powershell_executable)
    message(FATAL_ERROR "Source style check needs pwsh or powershell on PATH.")
endif()

execute_process(
    COMMAND "${powershell_executable}"
        -NoProfile
        -ExecutionPolicy Bypass
        -File "${ROOT_DIRECTORY}/scripts/check_source_style.ps1"
        -root "${ROOT_DIRECTORY}"
    RESULT_VARIABLE style_result
    COMMAND_ECHO STDOUT)
if(NOT style_result EQUAL 0)
    message(FATAL_ERROR "Source style check failed with exit code: ${style_result}")
endif()
