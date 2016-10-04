import gi
gi.require_version('Gst', '1.0')
gi.require_version('GstRtspServer', '1.0')
from gi.repository import Gst, GObject, GstRtspServer

GObject.threads_init()
Gst.init(None)


class RTSP_Server:
    def __init__(self):
        self.server = GstRtspServer.RTSPServer.new()
        self.address = '127.0.0.1'
        self.port = '8554'
        self.launch_description = '( playbin uri=file:///c://Temp//sample_video.mp4 )'
        self.server.set_address(self.address)
        self.server.set_service(self.port)
        self.server.connect("client-connected",self.client_connected) 
        self.factory = GstRtspServer.RTSPMediaFactory.new()
        self.factory.set_launch(self.launch_description)
        self.factory.set_shared(True)
        self.factory.set_transport_mode(GstRtspServer.RTSPTransportMode.PLAY)
        self.mount_points = self.server.get_mount_points()
        self.mount_points.add_factory('/video', self.factory)

        self.server.attach(None)  
        print('Stream ready')
        GObject.MainLoop().run()

    def client_connected(self, arg1, arg2):
        print('Client connected')


server = RTSP_Server()