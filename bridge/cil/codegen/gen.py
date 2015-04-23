#!/usr/bin/env python
import json
import os
from pprint import pprint
from Cheetah.Template import Template

def render( gens, tmpl_dir, output_dir ):

    prev_output_fn   = gens[0][1]
    prev_output      = ""
    count = (len(gens)-1)
    for c, (tmpl_fn, output_fn, data) in enumerate(gens):   # Concat the rendered template into output_fn
        t_tmpl  = Template(file= "%s%s" % (tmpl_dir, tmpl_fn), searchList=[{
            'data': data,
            'tmpl_dir': tmpl_dir
        }])
        last = count == c

        if (output_fn != prev_output_fn ):
            open( output_dir + prev_output_fn, 'w').write( str(prev_output) )
            prev_output = ""

        prev_output += str(t_tmpl)

        if last:
            open( output_dir + output_fn, 'w').write( str(prev_output) )

        prev_output_fn = output_fn

def map_type(typename, types):
    for t in types:
        if typename in t:
            return t

    return "ERR"

def main():

    script_dir  = "." + os.sep + "codegen" + os.sep
    output_dir  = script_dir + "output" + os.sep
    tmpl_dir    = script_dir + "templates" + os.sep

    if not os.path.exists(output_dir):
        os.makedirs(output_dir)

    reductions  = json.loads(open(script_dir+'reductions.json').read())
    opcodes     = json.loads(open(script_dir+'..'+ os.sep+'..'+ os.sep +'..'+ os.sep +'core'+ os.sep +'codegen'+ os.sep +'opcodes.json').read())
    types       = json.loads(open(script_dir+'..'+ os.sep+'..'+ os.sep + 'cpp' + os.sep + 'codegen' + os.sep +'element_types.json').read())

    operators   = json.loads(open(script_dir +'operators.json').read())

    op_map  = []
    for name, opcode, t, cilname in operators:
        code = [x for x in opcodes if x['opcode'] == opcode and not x['system_opcode']]

        typesigs = [x["types"] for x in opcodes if x['opcode'] == opcode and not x['system_opcode']]
        typesigs = typesigs[0] if typesigs else []
        
        new_typesigs = []
        for ttt in typesigs:
            sig = [map_type(typesig,types)  for typesig in ttt]
            new_typesigs.append(sig)

        typesigs = new_typesigs

        opcode_base, nop = opcode.split("_", 1)
        if opcode_base == "CUSTOM":
            opcode  = opcode_base
            nop     = int(nop)
        elif code:
            nop = code[0]["nop"]
        else:
            print "The Bohrium opcodes no longer include [ %s ]." % opcode
            continue

        op_map.append((name, opcode, t, nop, typesigs, cilname))

    gens = [
        ('PInvoke-basics.ctpl',         'PInvoke-basics.cs',            (types, reductions)),
        ('BohriumDataAccessor.ctpl',    'BohriumDataAccessor.cs',       (types, reductions)),
        ('TypedApplyImplementors.ctpl', 'TypedApplyImplementors.cs',    (types, reductions, op_map)),
        ('BohriumAccessorFactory.ctpl', 'BohriumAccessorFactory.cs',    types),
        ('PInvoke-operations.ctpl',     'PInvoke-operations.cs',        (types, op_map))
    ]

    render( gens, tmpl_dir, output_dir )

if __name__ == "__main__":
    main()
