cmake_minimum_required(VERSION 3.25.1)

project(wifibroadcast_listener C)

## Compile Options
option(ENABLE_GSTREAMER "Enable Video decoding using gstreamer" OFF)
if (ENABLE_GSTREAMER)
	add_definitions(-DENABLE_GSTREAMER)
endif ()

if (${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
	set (CMAKE_EXE_LINKER_FLAGS
		"${CMAKE_EXE_LINKER_FLAGS} -Wl,-no_warn_duplicate_libraries"
	)
	set (CMAKE_SHARED_LINKER_FLAGS
		"${CMAKE_SHARED_LINKER_FLAGS} -Wl,-no_warn_duplicate_libraries"
	)
endif ()

## packages defined by pkg-config
find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBEVENT REQUIRED libevent)
pkg_check_modules(LIBPCAP REQUIRED libpcap)
pkg_check_modules(LIBSODIUM REQUIRED libsodium)
#pkg_check_modules(LIBEDIT REQUIRED libedit)
if (ENABLE_GSTREAMER)
	# libgstreamer-1.0-dev
	pkg_check_modules(GSTREAMER REQUIRED gstreamer-1.0)
	# libgstreamer-plugins-base1.0-dev
	pkg_check_modules(GSTREAMER_APP REQUIRED gstreamer-app-1.0)
	# libgstreamer-plugins-base1.0-dev
	pkg_check_modules(GSTREAMER_VIDEO REQUIRED gstreamer-video-1.0)
	if (CMAKE_SYSTEM_NAME STREQUAL "Linux")
		# cairo
		pkg_check_modules(CAIRO REQUIRED cairo)
	endif ()
endif ()

## libyaml
FIND_PATH(LIBYAML_INCLUDE_DIR NAMES yaml.h)
FIND_LIBRARY(LIBYAML_LIBRARIES NAMES yaml libyaml)

## External module radiotap-libary
set (RADIOTAP_DIR "${CMAKE_SOURCE_DIR}/external/radiotap-library")
set (RADIOTAP_INCLUDE_DIRS "${RADIOTAP_DIR}")
set (RADIOTAP_SOURCES "${RADIOTAP_DIR}/radiotap.c")
set_source_files_properties("${RADIOTAP_DIR}/radiotap.c"
	PROPERTIES COMPILE_FLAGS "-Wno-address-of-packed-member")

## External module zfec
set (ZFEC_DIR "${CMAKE_SOURCE_DIR}/external/zfec/zfec")
set (ZFEC_INCLUDE_DIRS "${ZFEC_DIR}")
set (ZFEC_SOURCES "${ZFEC_DIR}/fec.c")

## External module yyjson
set (YYJSON_DIR "${CMAKE_SOURCE_DIR}/external/yyjson")
add_subdirectory(${YYJSON_DIR})

## Thread
set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

## wfb settings
set (WFB_CFLAGS_OTHER
	"-Wall"
	"-g"
)

set(wfb_listener_srcs
	src/main.c
	src/net_core.c
	src/net_pcap.c
	src/net_inet.c
	src/rx_core.c
	src/rx_session.c
	src/rx_data.c
	src/rx_log.c
	src/frame_udp.c
	src/frame_pcap.c
	src/frame_radiotap.c
	src/frame_ieee80211.c
	src/frame_wfb.c
	src/crypto_wfb.c
	src/fec_wfb.c
	src/util_msg.c
	src/util_rbuf.c
	src/util_inet.c
	src/wfb_ipc.c
	src/compat.c
	src/daemon.c
	${RADIOTAP_SOURCES}
	${ZFEC_SOURCES}
)

set (wfb_listener_incs
	${ZFEC_INCLUDE_DIRS}
	${RADIOTAP_INCLUDE_DIRS}
	${LIBEVENT_INCLUDE_DIRS}
	${LIBPCAP_INCLUDE_DIRS}
	${LIBSODIUM_INCLUDE_DIRS}
)

set (wfb_listener_libdirs
	${LIBEVENT_LIBRARY_DIRS}
	${LIBPCAP_LIBRARY_DIRS}
	${LIBSODIUM_LIBRARY_DIRS}
)

set (wfb_listener_libs
	${LIBEVENT_LIBRARIES}
	${LIBPCAP_LIBRARIES}
	${LIBSODIUM_LIBRARIES}
)

set (wfb_listener_cflags
	${LIBEVENT_CFLAGS_OTHER}
	${LIBPCAP_CFLAGS_OTHER}
	${LIBSODIUM_CFLAGS_OTHER}
	${WFB_CFLAGS_OTHER}
	${CMAKE_THREAD_LIBS_INIT}
)

if (ENABLE_GSTREAMER)
	set (wfb_listener_srcs
		${wfb_listener_srcs}
		src/wfb_gst.c
		src/wfb_gst_overlay.c
	)
	set (wfb_listener_incs
		${wfb_listener_incs}
		${GSTREAMER_INCLUDE_DIRS}
		${CAIRO_INCLUDE_DIRS}
	)
	set (wfb_listener_libdirs
		${wfb_listener_libdirs}
		${GSTREAMER_LIBRARY_DIRS}
		${CAIRO_LIBRARY_DIRS}
	)
	set (wfb_listener_libs
		${wfb_listener_libs}
		${GSTREAMER_LIBRARIES}
		${GSTREAMER_APP_LIBRARIES}
		${GSTREAMER_VIDEO_LIBRARIES}
		${CAIRO_LIBRARIES}
	)
	set (wfb_listener_cflags
		${wfb_listener_cflags}
		${GSTREAMER_CFLAGS_OTHER}
	)
endif ()

add_executable(wfb_listener
	${wfb_listener_srcs}
)

set_target_properties(wfb_listener PROPERTIES C_STANDARD 99)

target_include_directories(wfb_listener PUBLIC
	${wfb_listener_incs}
)

target_link_directories(wfb_listener PUBLIC
	${wfb_listener_libdirs}
)

target_link_libraries(wfb_listener
	${wfb_listener_libs}
)

target_compile_options(wfb_listener PUBLIC
	${wfb_listener_cflags}
)

install(TARGETS wfb_listener
)
if (CMAKE_SYSTEM_NAME STREQUAL "Linux")
	install(CODE "execute_process (COMMAND setcap 'CAP_NET_RAW+eip CAP_NET_ADMIN+eip' $<TARGET_FILE:wfb_listener>)"
)
endif ()

set(wfb_log_analysis_srcs
	src/log_analysis/log_analysis.c
	src/log_analysis/log_raw.c
	src/log_analysis/log_csv.c
	src/log_analysis/log_json.c
	src/log_analysis/log_summary.c
	src/log_analysis/log_message.c
	src/log_analysis/log_hist.c
	src/log_analysis/log_filter.c
	src/log_analysis/shell.c
	src/util_msg.c
	src/compat.c
)

set(wfb_log_analysis_libs
	${LIBYAML_LIBRARIES}
)

set(wfb_log_analysis_incs
	${wfb_listener_incs}
)

set(wfb_log_analysis_libdirs
	${wfb_listener_libdirs}
)

if (ENABLE_GSTREAMER)
set(wfb_log_analysis_srcs
	${wfb_log_analysis_srcs}
	src/wfb_gst.c
	src/wfb_gst_overlay.c
	src/log_analysis/log_h265.c
)
set(wfb_log_analysis_libs
	${wfb_log_analysis_libs}
	${GSTREAMER_APP_LIBRARIES}
	${GSTREAMER_VIDEO_LIBRARIES}
	${CAIRO_LIBRARIES}
)
set(wfb_log_analysis_incs
	${wfb_log_analysis_incs}
	${CAIRO_INCLUDE_DIRS}
)
set(wfb_log_analysis_libdirs
	${wfb_log_analysis_libdirs}
	${CAIRO_LIBRARY_DIRS}
)
endif ()

add_executable(wfb_log_analysis
	${wfb_log_analysis_srcs}
)

target_include_directories(wfb_log_analysis PUBLIC
	${wfb_log_analysis_incs}
	${LIBYAML_INCLUDE_DIR}
)

target_link_directories(wfb_log_analysis PUBLIC
	${wfb_log_analysis_libdirs}
)

target_link_libraries(wfb_log_analysis PUBLIC
	${wfb_log_analysis_libs}
)

target_link_libraries(wfb_log_analysis PRIVATE
	yyjson
)

install(TARGETS wfb_log_analysis
)
