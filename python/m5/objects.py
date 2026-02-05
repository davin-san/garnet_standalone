from .params import *

class SimObject:
    def __init__(self, **kwargs):
        for k, v in kwargs.items():
            setattr(self, k, v)

class ClockedObject(SimObject): pass

class Router(SimObject):
    def __init__(self, **kwargs):
        super().__init__(**kwargs)
        self.router_id = kwargs.get('router_id')
        self.latency = kwargs.get('latency')

class IntLink(SimObject):
    def __init__(self, **kwargs):
        super().__init__(**kwargs)
        self.link_id = kwargs.get('link_id')
        self.src_node = kwargs.get('src_node')
        self.dst_node = kwargs.get('dst_node')
        self.src_outport = kwargs.get('src_outport')
        self.dst_inport = kwargs.get('dst_inport')
        self.latency = kwargs.get('latency')
        self.weight = kwargs.get('weight')

class ExtLink(SimObject):
    def __init__(self, **kwargs):
        super().__init__(**kwargs)
        self.link_id = kwargs.get('link_id')
        self.ext_node = kwargs.get('ext_node')
        self.int_node = kwargs.get('int_node')
        self.latency = kwargs.get('latency')
