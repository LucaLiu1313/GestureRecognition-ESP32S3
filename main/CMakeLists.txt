FILE(GLOB_RECURSE app_sources ${CMAKE_SOURCE_DIR}/main/*.*)

# 定义目标源文件
idf_component_register(SRCS "station_upload.c" 
                    INCLUDE_DIRS "."
                    REQUIRES esp_camera
                    REQUIRES nvs_flash
                    REQUIRES esp_http_server
                    REQUIRES esp_http_client 
                    esp_camera)