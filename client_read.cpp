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
 // setting serial is not possible because message is already locked.
 // the following will just emit a warning and does nothing:
  // message->set_serial(out_serial);
  return Glib::wrap(result);  
}

std::vector<guint32> get_vector_pipe(Glib::RefPtr<Gio::DBus::Connection> connection)
{
  // connect to the C++ (Gio::DBus) server
  Glib::RefPtr<Gio::DBus::Message> message = 
    Gio::DBus::Message::create_method_call(
      "org.glibmm.DBusExample",        // service name
      "/org/glibmm/DBus/TestObject",    // object path
      "org.glibmm.DBusExample.Machine", // interface name
      "GetArray"                     // method name
    );
   // std::cerr << "message.get_locked() " << message->get_locked() << std::endl;

   gint timeout = -1; // [ms]
   //std::cout << "sending message" << std::endl;
   Glib::RefPtr<Gio::DBus::Message> reply = 
     connection_send_message_with_reply_sync(connection, message, timeout);

    // std::cerr << "message.get_locked() " << message->get_locked() << std::endl;
    // std::cerr << "reply.get_locked() " << reply->get_locked() << std::endl;

   //std::cout << "get_unix_fd_list()" << std::endl;
   //  std::cerr << "before get_unix_fd_list" << std::endl;
   //Glib::RefPtr<Gio::UnixFDList> fd_list = reply->get_unix_fd_list();
    GUnixFDList *fd_list = g_dbus_message_get_unix_fd_list(reply->gobj());

   //std::cerr << "after get_unix_fd_list" << std::endl;
   if (fd_list == 0)
   {
     std::cerr << "didn't get fd_list ..." << std::endl;
     exit(1);
   }
   //std::cout << "before fd_list->get()" << std::endl;
   gint fd = g_unix_fd_list_get(fd_list,0,0);
   //std::cout << "after fd_list->get()" << std::endl;

   guint size;
   read(fd,&size,sizeof(size));
  // std::cout << "have fd" << std::endl;
  //  std::cout << "done fd = " << fd << std::endl;

    //int size = 100;
   std::vector<guint32> block(size,size);
   int nbytes = read(fd,&block[0],sizeof(guint32)*(block.size()));

   close(fd);

 //  std::cout << "nbytes=" << nbytes << "  "  << block[0] << " " << block[size-1] << std::endl;


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
    Gio::DBus::Connection::get_sync(Gio::DBus::BUS_TYPE_SYSTEM); 

  std::cerr << "have connection" << std::endl;  
  sleep(5);

  Glib::RefPtr<Gio::Cancellable> cancellable;
  auto proxy = Gio::DBus::Proxy::create_sync(
                    connection, 
                    "org.glibmm.DBusExample", 
                    "/org/glibmm/DBus/TestObject",
                    "org.glibmm.DBusExample.Machine", 
                    cancellable);

  std::cerr << "have proxy" << std::endl;  
  sleep(5);

  std::ofstream data_pipe ("measurment_pipe.dat");
  std::ofstream data_array("measurment_array.dat");


  for (int i = 0; i < 1; ++i)
  {
    if (i && !(i%1000)) std::cerr << i/100 << "\%"<< std::endl;
    //std::cerr << "run : " << i << std::endl;
    timespec ts_start, ts_stop;

    ////////////////////////////////////////////
    // measurement with pipe via file descriptor
    ////////////////////////////////////////////
    {
      clock_gettime(CLOCK_REALTIME, &ts_start);
      std::vector<guint32> block = get_vector_pipe(connection);
      clock_gettime(CLOCK_REALTIME, &ts_stop);
      data_pipe << block.size() << " " << delta_t_ms(ts_start,ts_stop) << std::endl;
      //std::cerr << block.size() << " " << delta_t_ms(ts_start,ts_stop) << std::endl;
    }
    ////////////////////////////
    // measurement without pipe
    ////////////////////////////
    {
      clock_gettime(CLOCK_REALTIME, &ts_start);
      std::vector<guint32> block = get_vector_array(proxy);
      clock_gettime(CLOCK_REALTIME, &ts_stop);
      data_array << block.size() << " " << delta_t_ms(ts_start,ts_stop) << std::endl;
    }
  }  

  std::cerr << "done" << std::endl;
  sleep(5);


  return 0; 
}