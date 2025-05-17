cmake_minimum_required(VERSION 3.25.1)

project(wifibroadcast_listener C)

## packages defined by pkg-config
find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBEVENT REQUIRED libevent)
pkg_check_modules(LIBPCAP REQUIRED libpcap)
pkg_check_modules(LIBSODIUM REQUIRED libsodium)
pkg_check_modules(GSTREAMER REQUIRED gstreamer-1.0)

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


## Thread
set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

## wfb settings
set (WFB_CFLAGS_OTHER
	"-Wall"
	"-g"
)

add_executable(wfb_listener
	src/main.c
	src/net_core.c
	src/net_pcap.c
	src/net_inet6.c
	src/rx_core.c
	src/rx_session.c
	src/rx_data.c
	src/frame_pcap.c
	src/frame_radiotap.c
	src/frame_ieee80211.c
	src/frame_wfb.c
	src/crypto_wfb.c
	src/fec_wfb.c
	src/decode_h265.c
	src/util_log.c
	src/util_rbuf.c
	src/compat.c
	${RADIOTAP_SOURCES}
	${ZFEC_SOURCES}
)

if (CMAKE_SYSTEM_NAME STREQUAL "Linux")
	add_custom_target(set_cap
		sudo setcap 'CAP_NET_RAW+eip CAP_NET_ADMIN+eip' wfb_listener
	)
endif ()

set_target_properties(wfb_listener PROPERTIES C_STANDARD 99)

target_include_directories(wfb_listener PUBLIC
	${ZFEC_INCLUDE_DIRS}
	${RADIOTAP_INCLUDE_DIRS}
	${LIBEVENT_INCLUDE_DIRS}
	${LIBPCAP_INCLUDE_DIRS}
	${LIBSODIUM_INCLUDE_DIRS}
	${GSTREAMER_INCLUDE_DIRS}
)

target_link_directories(wfb_listener PUBLIC
	${LIBEVENT_LIBRARY_DIRS}
	${LIBPCAP_LIBRARY_DIRS}
	${LIBSODIUM_LIBRARY_DIRS}
	${GSTREAMER_LIBRARY_DIRS}
)

target_link_libraries(wfb_listener
	${LIBEVENT_LIBRARIES}
	${LIBPCAP_LIBRARIES}
	${LIBSODIUM_LIBRARIES}
	${GSTREAMER_LIBRARIES}
)

target_compile_options(wfb_listener PUBLIC
	${LIBEVENT_CFLAGS_OTHER}
	${LIBPCAP_CFLAGS_OTHER}
	${LIBSODIUM_CFLAGS_OTHER}
	${GSTREAMER_CFLAGS_OTHER}
	${WFB_CFLAGS_OTHER}
	${CMAKE_THREAD_LIBS_INIT}
)
