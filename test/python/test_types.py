import bohrium as np
from numpytest import numpytest,TYPES


class test_different_inputs(numpytest):
    
    def init(self):
        for t1 in TYPES.ALL:
            for t2 in TYPES.ALL:
                a = {}
                v =  "a[0] = self.array((10),%s);"%(t1)
                v += "a[1] = self.array((10),%s);"%(t2)
                exec(v)
                yield (a,v)

    def test_typecast(self,a):
        cmd = "res = a[0] + a[1]"
        exec(cmd)
        return (res,cmd)      