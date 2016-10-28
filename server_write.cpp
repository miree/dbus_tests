/* Copyright (C) 2011 The giomm Development Team
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free
 * Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

/* This is a basic server providing a clock like functionality.  Clients can
 * get the current time, set the alarm and get notified when the alarm time is
 * reached.  It is basic because there is only one global alarm which any
 * client can set.  Clients listening for the alarm signal will be notified by
 * use of the global alarm signal.  The server should be easily modifiable to
 * allow per-client alarms, but that is left as an exercise.
 *
 * Along with the above it provides a method to get its stdout's file
 * descriptor to test the Gio::DBus::Message API.
 */

#include <giomm.h>
#include <glibmm.h>
#include <iostream>
#include <unistd.h>

namespace
{

static Glib::RefPtr<Gio::DBus::NodeInfo> introspection_data;

static Glib::ustring introspection_xml = "<node>"
                                         "  <interface name='org.glibmm.DBusExample.Machine'>"
                                         "    <method name='GetTheAnswer'>"
                                         "      <arg type='u' name='answer' direction='out'/>"
                                         "    </method>"
                                         "    <method name='GetArray'/>"
                                         "    <method name='GetArrayNoPipe'>"
                                         "      <arg type='au' name='result' direction='out'/>"
                                         "    </method>"
                                         "  </interface>"
                                         "</node>";

guint registered_id = 0;

int fildes[2] = {0,0}; //fildes[0] is read end, fildes[1] is write end

} // anonymous namespace
 
static void
on_method_call(const Glib::RefPtr<Gio::DBus::Connection>& connection ,
  const Glib::ustring& /* sender */, const Glib::ustring& /* object_path */,
  const Glib::ustring& /* interface_name */, const Glib::ustring& method_name,
  const Glib::VariantContainerBase& parameters,
  const Glib::RefPtr<Gio::DBus::MethodInvocation>& invocation)
{
  //std::cerr << "server: on_method_call " << method_name << std::endl;
  if (method_name == "GetTheAnswer")
  {
    //std::cerr << "server: GetTheAnswer called" << std::endl;

    const auto answer  = Glib::Variant< guint >::create(42);
    // Create the tuple.
    Glib::VariantContainerBase response = Glib::VariantContainerBase::create_tuple(answer);

    // Return the tuple with the included time.
    invocation->return_value(response);
  }
  else if (method_name == "GetArrayNoPipe")
  {
    guint32 size = rand()%10000;
    std::vector< guint32 > array(size);
    std::vector<Glib::VariantBase> response_vector;
    response_vector.push_back(Glib::Variant< std::vector< guint32 > >::create(array));    
    invocation->return_value(Glib::VariantContainerBase::create_tuple(response_vector));
  }
  else if (method_name == "GetArray")
  {
    guint32 size = 1 + rand()%10000;
    //std::cerr << "server: GimmeStdout called" << std::endl;
    //std::cerr << "server: create fd_list" << std::endl;
    Glib::RefPtr<Gio::UnixFDList> fd_list = Gio::UnixFDList::create();
    //std::cerr << "server: append to fd_list STDOUT_FILENO = " << STDOUT_FILENO << std::endl;
    //fd_list->append(STDOUT_FILENO);
    //std::cerr << "fildes[0] = " << fildes[0] << std::endl;
    //std::cerr << "fildes[1] = " << fildes[1] << std::endl;

    int err = fd_list->append(fildes[0]);
    //std::cerr << "fd index = " << err << std::endl;
    if (err == -1 ) // error
    {
      std::cerr << "could not append file desctiptor to list" << std::endl;
    }
    Glib::RefPtr<Gio::DBus::Message> reply = invocation->get_message();
    //reply->set_unix_fd_list(fd_list); // is done inside invocation->return_value
    //connection->send_message(reply);  // is also done in invocation->return_value
    Glib::VariantContainerBase response;
    invocation->return_value(response,fd_list);
    //std::cerr << "writing" << std::endl;
    std::vector<guint32> block(size,42);
    block[size-1]=41;
    int nbytes = write(fildes[1], &size, sizeof(guint32));
    nbytes = write(fildes[1], &block[0],sizeof(guint32)*block.size()); // write to write end of pipe
    //std::cerr << "witing done " << nbytes << " bytes" << std::endl;
  }
  else
  {
    std::cerr << "server: unknown method name" << std::endl;
    // Non-existent method on the interface.
    Gio::DBus::Error error(Gio::DBus::Error::UNKNOWN_METHOD, "Method does not exist.");
    invocation->return_error(error);
  }
}

// This must be a global instance. See the InterfaceVTable documentation.
// TODO: Make that unnecessary.
const Gio::DBus::InterfaceVTable interface_vtable(sigc::ptr_fun(&on_method_call));

void
on_bus_acquired(
  const Glib::RefPtr<Gio::DBus::Connection>& connection, const Glib::ustring& /* name */)
{
  // Export an object to the bus:

  // See https://bugzilla.gnome.org/show_bug.cgi?id=646417 about avoiding
  // the repetition of the interface name:
  try
  {
    registered_id = connection->register_object(
      "/org/glibmm/DBus/TestObject", introspection_data->lookup_interface(), interface_vtable);
  }
  catch (const Glib::Error& ex)
  {
    std::cerr << "Registration of object failed." << std::endl;
  }

  return;
}

void
on_name_acquired(
  const Glib::RefPtr<Gio::DBus::Connection>& /* connection */, const Glib::ustring& /* name */)
{
  // TODO: What is this good for? See https://bugzilla.gnome.org/show_bug.cgi?id=646427
}

void
on_name_lost(const Glib::RefPtr<Gio::DBus::Connection>& connection, const Glib::ustring& /* name */)
{
  std::cerr << "name lost" << std::endl;
  connection->unregister_object(registered_id);
}

int
main(int, char**)
{
  std::locale::global(std::locale(""));
  Gio::init();

  int status = pipe(fildes);

  if (status==-1)
  {
    std::cerr << "Cannot create pipe" << std::endl;
    return 1;
  }

  try
  {
    introspection_data = Gio::DBus::NodeInfo::create_for_xml(introspection_xml);
  }
  catch (const Glib::Error& ex)
  {
    std::cerr << "Unable to create introspection data: " << ex.what() << "." << std::endl;
    return 1;
  }

  const auto id = Gio::DBus::own_name(Gio::DBus::BUS_TYPE_SYSTEM, 
                                      "org.glibmm.DBusExample",
                                      sigc::ptr_fun(&on_bus_acquired), 
                                      sigc::ptr_fun(&on_name_acquired),
                                      sigc::ptr_fun(&on_name_lost));

  // Keep the service running until the process is killed:
  auto loop = Glib::MainLoop::create();
  loop->run();

  Gio::DBus::unown_name(id);

  return EXIT_SUCCESS;
}

