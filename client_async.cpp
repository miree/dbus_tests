#include <giomm.h>
#include <glibmm.h>
#include <iostream>
#include <fstream>
#include <ctime>
#include <cmath>
#include <gio/gunixfdlist.h>

////////////////////////////////////////////
// Get an array from a DBus service call
// 
////////////////////////////////////////////
std::vector<guint32> get_vector_array(Glib::RefPtr<Gio::DBus::Proxy> proxy)
{
  Glib::RefPtr<Gio::Cancellable> cancellable;
  const Glib::VariantContainerBase& response = proxy->call_sync("GetArrayNoPipe", cancellable);
  Glib::Variant< std::vector< guint32 > > array;
  response.get_child(array, 0);
  return array.get();
}

////////////////////////////////////////////
// Get an array through a pipe where the
//  file descriptors are passed via DBus
//  => is this a bug in the Glibmm-wrapper that
//     is worth posting in the bug tracker ???
////////////////////////////////////////////
Glib::RefPtr<Gio::DBus::Message> connection_send_message_with_reply_sync(
    Glib::RefPtr<Gio::DBus::Connection> connection,
    Glib::RefPtr<Gio::DBus::Message> message,
    gint timeout_msec)
{
  volatile guint32 out_serial = 0;
  GError* gerror = nullptr;

  GDBusMessage* result = g_dbus_connection_send_message_with_reply_sync(connection->gobj(),
    Glib::unwrap(message), static_cast<GDBusSendMessageFlags>(message->get_flags()), timeout_msec,
    &out_serial, nullptr, &gerror);

  if (gerror)
    ::Glib::Error::throw_exception(gerror);
 // setting serial is not possible because message is already locked (always).
 //  the following will just have no effect but emit a warning:
  // message->set_serial(out_serial);   //  <== BUG????? need other way of transporting out_serial to the caller?
  return Glib::wrap(result);  
}

void on_async_call_ready(Glib::RefPtr<Gio::AsyncResult>& async_result, Glib::RefPtr<Gio::DBus::Connection> connection, Glib::VariantContainerBase *result, bool *ready)
{
  // At this point, both participants (client and service) have both ends of the pipe.
  // Now, if arrays are to be used as input parameter on the service side, they can be transferred - one after the other - through the pipe.
  //
  // this function should have a (const std::vector<T> &data) argument
  // int size = data.size();
  // write(pipe_input, &size, sizeof(size));
  // write(pipe_input, &data[0], sizeof(T)*size);
  //
  // The service side will wait for data to arrive at the pipe output. This data will be put into std::vector<T>, and the service function will be called.
  //  After the service funciton was called result vectors are available.

  std::cerr << "on_async_call_ready" << std::endl;
  *result = connection->call_finish(async_result);  // this yields the "normal" parameters from the remote function call
  std::cerr << "on_async_call_ready: call_finish done" << std::endl;
  *ready = true;

  // Now, (in case we expect vector types resulting from the service function, we have to wait for data to arrive at the output side of the pipe.
  // We will fill this data into a std::vector<T'> result_data 
  //
  // int result_size;
  // this function should have a (std::vector<T> &result_data) argument
  // read(pipe_output, &result_size, sizeof(size));
  // result_data.resize(size);
  // read(pipe_output, &result_data[0], sizeof(T')*size);
}

std::vector<guint32> get_vector_pipe(Glib::RefPtr<Gio::DBus::Connection> connection)
{
  // create pipe
  int fildes[2]; // [1] is write-end of pipe; [2] is read-end of pipe
  int status = pipe(fildes);

  if (status==-1)
  {
    std::cerr << "Cannot create pipe" << std::endl;
    exit(1);
  }
  std::cerr << "fildes[0] = " << fildes[0] << "    fildes[1] = " << fildes[1] << std::endl;


  ////////////////////////////////////////////////////////////////////////////////
  // Glibmm-version
  ////////////////////////////////////////////////////////////////////////////////
  Glib::VariantContainerBase      parameters;
  Glib::RefPtr<Gio::Cancellable> cancellable;
  Glib::RefPtr<Gio::UnixFDList>   fd_list = Gio::UnixFDList::create();
  fd_list->append(fildes[0]);
  fd_list->append(fildes[1]);
  Glib::RefPtr<Gio::UnixFDList>   out_fd_list;
  int                             timeout_msec = -1;
  Gio::DBus::CallFlags            flags;
  Glib::VariantType               reply_type;
  bool call_ready = false;

  Glib::VariantContainerBase result;

  Glib::RefPtr<Glib::MainLoop> mainloop = Glib::MainLoop::create();
  Glib::RefPtr<Glib::MainContext> context = mainloop->get_context();

  connection->call(
        "/org/glibmm/DBus/TestObject",    // object path
        "org.glibmm.DBusExample.Machine", // interface name
        "GetArray",                       // method name
        parameters,
        sigc::bind(sigc::bind(sigc::bind(sigc::ptr_fun(&on_async_call_ready), &call_ready), &result), connection),
        cancellable,
        fd_list,
        "org.glibmm.DBusExample",
        timeout_msec,
        flags,
        reply_type
    );

  int i = 0;
  while(!context->iteration(true) || !call_ready) // wait unitl the d-bus call was answered (i.e., "on_async_call_ready" was called)
  {
    ++i; 
    std::cerr << "tick" << std::endl;
  }
  std::cerr << "waited " << i << " rounds" << std::endl;

  ////////////////////////////////////////////////////////////////////////////////
  // GLib-version ( has a memory leak ... 
  //                    ...which I'm not able to find without studying 
  //                          the Glib Memory philosopy ... )
  ////////////////////////////////////////////////////////////////////////////////
  // //Glib::VariantContainerBase parameters;
  // GVariant                   *parameters = nullptr;
  // //Glib::VariantType          reply_type;
  // GVariantType              *reply_type = nullptr;
  // Gio::DBus::CallFlags            flags;
  // int                        timeout_msec = -1;
  // GError                     *gerror = nullptr;
  // GUnixFDList                *fd_list = g_unix_fd_list_new();
  // GUnixFDList                *out_fd_list;
  // // fill the fd_list
  // gint stat = g_unix_fd_list_append(fd_list, fildes[0], &gerror);
  // if (gerror != NULL)
  // {
  //   std::cerr  << "fd_list_append fildes[0] error: " << gerror->message << std::endl;
  //   g_free(gerror);
  //   gerror = NULL;
  // }

  // stat = g_unix_fd_list_append(fd_list, fildes[1], &gerror);
  // if (gerror != NULL)
  // {
  //   std::cerr  << "fd_list_append fildes[1] error: " << gerror->message << std::endl;
  //   g_free(gerror);
  //   gerror = NULL;
  // }

  // GVariant *result = g_dbus_connection_call_with_unix_fd_list_sync(
  //                       connection->gobj(),
  //                       "org.glibmm.DBusExample",
  //                       "/org/glibmm/DBus/TestObject",    // object path
  //                       "org.glibmm.DBusExample.Machine", // interface name
  //                       "GetArray",                     // method name
  //                       parameters, //const_cast<GVariant*>(parameters.gobj()),
  //                       reply_type, //reply_type.gobj(),
  //                       static_cast<GDBusCallFlags>(flags),
  //                       timeout_msec,
  //                       fd_list,
  //                       &out_fd_list,
  //                       nullptr,
  //                       &gerror
  //                    );
  // if (gerror != NULL)
  // {
  //   std::cerr << "there was an errror " << std::endl;
  // }
  // // close duplicated file descriptors and free list recources
  //  gint len;
  //  int *fd_from_list = g_unix_fd_list_steal_fds(fd_list,&len);
  //  std::cerr << "got " << len << " fds from list " << std::endl;
  //  close(fd_from_list[0]);
  //  close(fd_from_list[1]);
  //  g_free(fd_from_list);


  //   g_variant_unref(parameters);
  //   g_variant_type_free(reply_type);
  

  //   // read size
     guint size = 100;
     read(fildes[0],&size,sizeof(size));
  //   // read block
     std::vector<guint32> block(size,size);
     int nbytes = read(fildes[0],&block[0],sizeof(guint32)*(block.size()));
    std::cout << "nbytes=" << nbytes << "  "  << block[0] << " " << block[size-1] << std::endl;

   close(fildes[0]);
   close(fildes[1]);
  return block;

}

double delta_t_ms(timespec ts_start, timespec ts_stop)
{
  return 1e3*(ts_stop.tv_sec - ts_start.tv_sec) + (ts_stop.tv_nsec - ts_start.tv_nsec) / 1e6; 
}

int main(int, char**) { 
  Gio::init(); 
  // Get the bus connection.
  Glib::RefPtr<Gio::DBus::Connection> connection = 
    Gio::DBus::Connection::get_sync(Gio::DBus::BUS_TYPE_SESSION); 

  std::cerr << "have connection" << std::endl;  

  Glib::RefPtr<Gio::Cancellable> cancellable;
  auto proxy = Gio::DBus::Proxy::create_sync(
                    connection, 
                    "org.glibmm.DBusExample", 
                    "/org/glibmm/DBus/TestObject",
                    "org.glibmm.DBusExample.Machine", 
                    cancellable);

  std::cerr << "have proxy" << std::endl;  

  std::ofstream data_pipe ("measurment_pipe.dat");
  std::ofstream data_array("measurment_array.dat");



  for (int i = 0; i < 1; ++i)
  {
    if (i && !(i%1000)) std::cerr << i/100 << "\%"<< std::endl;
    std::cerr << "run : " << i << std::endl;
    timespec ts_start, ts_stop;

    ////////////////////////////////////////////
    // measurement with pipe via file descriptor
    ////////////////////////////////////////////
    {
      clock_gettime(CLOCK_REALTIME, &ts_start);
      std::vector<guint32> block = get_vector_pipe(connection);
      clock_gettime(CLOCK_REALTIME, &ts_stop);
      data_pipe << block.size() << " " << delta_t_ms(ts_start,ts_stop) << std::endl;
      std::cerr << block.size() << " " << delta_t_ms(ts_start,ts_stop) << std::endl;
    }
    ////////////////////////////
    // measurement without pipe
    ////////////////////////////
    // {
    //   clock_gettime(CLOCK_REALTIME, &ts_start);
    //   std::vector<guint32> block = get_vector_array(proxy);
    //   clock_gettime(CLOCK_REALTIME, &ts_stop);
    //   data_array << block.size() << " " << delta_t_ms(ts_start,ts_stop) << std::endl;
    // }
  }  


  std::cerr << "done" << std::endl;

  return 0; 
}