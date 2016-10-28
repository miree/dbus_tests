CXXFLAGS = `pkg-config --libs --cflags giomm-2.4 glibmm-2.4 gio-unix-2.0`
all: client_read  server_write  \
	 client_write server_read   \
	 client server \
	 client_async server_async \
