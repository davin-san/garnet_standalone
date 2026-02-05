class Param: pass
class String(Param): pass
class Int(Param): pass
class Bool(Param): pass
class Float(Param): pass
class Vector(Param): pass
class Proxy(Param): pass
class Parent(Proxy): pass

class MemorySize:
    def __init__(self, val): pass
    def __floordiv__(self, other): return self
    def __truediv__(self, other): return self
