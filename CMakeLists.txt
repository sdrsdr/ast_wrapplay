project(ast_wrapplay)
cmake_minimum_required(VERSION 2.6)

set ( ASTERISK_INCLUDES "/usr/include" CACHE PATH "location of asterisk.h" )
set ( ASTERISK_MODULES "/usr/lib/asterisk/modules/" CACHE PATH "where to install the asterisk application" )

add_definitions(-Wall)


include_directories (${ASTERISK_INCLUDES})
add_library (app_wrapplay SHARED app_wrapplay.c)
add_library (app_mmp3 SHARED app_mmp3.c)

set_target_properties(app_wrapplay PROPERTIES PREFIX "" OUTPUT_NAME app_wrapplay)
set_target_properties(app_mmp3 PROPERTIES PREFIX "" OUTPUT_NAME app_mmp3)

install(TARGETS app_wrapplay  DESTINATION ${ASTERISK_MODULES} PERMISSIONS OWNER_READ OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)
install(TARGETS app_mmp3  DESTINATION ${ASTERISK_MODULES} PERMISSIONS OWNER_READ OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)
