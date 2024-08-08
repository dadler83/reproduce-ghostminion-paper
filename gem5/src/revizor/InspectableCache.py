from m5.params import *
from m5.SimObject import *
from m5.objects.Cache import Cache


class InspectableCache(Cache):
    type = "InspectableCache"
    cxx_header = "revizor/inspectable_cache.hh"
    cxx_class = "gem5::InspectableCache"
    cxx_exports = []
