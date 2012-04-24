﻿using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using NumCIL.Generic;
using System.Runtime.InteropServices;

namespace NumCIL.cphVB
{
    /// <summary>
    /// Basic factory for creating cphVB accessors
    /// </summary>
    /// <typeparam name="T">The type of data kept in the underlying array</typeparam>
    public class cphVBAccessorFactory<T> : NumCIL.Generic.IAccessorFactory<T>
    {
        public IDataAccessor<T> Create(long size) { return new cphVBAccessor<T>(size); }
        public IDataAccessor<T> Create(T[] data) { return new cphVBAccessor<T>(data); }
    }

    /// <summary>
    /// Code to map from NumCIL operations to cphVB operations
    /// </summary>
    public class OpCodeMapper
    {
        /// <summary>
        /// Lookup table with mapping from NumCIL operation name to cphVB opcode
        /// </summary>
        private static Dictionary<PInvoke.cphvb_opcode, string> _opcode_func_name;

        /// <summary>
        /// Static initializer, builds mapping table between the cphVB opcodes.
        /// and the corresponding names of the operations in NumCIL
        /// </summary>
        static OpCodeMapper()
        {
            _opcode_func_name = new Dictionary<PInvoke.cphvb_opcode, string>();

            _opcode_func_name.Add(PInvoke.cphvb_opcode.CPHVB_ADD, "Add");
            _opcode_func_name.Add(PInvoke.cphvb_opcode.CPHVB_SUBTRACT, "Sub");
            _opcode_func_name.Add(PInvoke.cphvb_opcode.CPHVB_MULTIPLY, "Mul");
            _opcode_func_name.Add(PInvoke.cphvb_opcode.CPHVB_DIVIDE, "Div");
            _opcode_func_name.Add(PInvoke.cphvb_opcode.CPHVB_MOD, "Mod");
            _opcode_func_name.Add(PInvoke.cphvb_opcode.CPHVB_MAXIMUM, "Max");
            _opcode_func_name.Add(PInvoke.cphvb_opcode.CPHVB_MINIMUM, "Min");

            //_opcode_func_name.Add(PInvoke.cphvb_opcode.CPHVB_INCREMENT, "Inc");
            //_opcode_func_name.Add(PInvoke.cphvb_opcode.CPHVB_DECREMENT, "Dec");
            _opcode_func_name.Add(PInvoke.cphvb_opcode.CPHVB_FLOOR, "Floor");
            _opcode_func_name.Add(PInvoke.cphvb_opcode.CPHVB_CEIL, "Ceiling");
            _opcode_func_name.Add(PInvoke.cphvb_opcode.CPHVB_RINT, "Round");

            _opcode_func_name.Add(PInvoke.cphvb_opcode.CPHVB_ABSOLUTE, "Abs");
            _opcode_func_name.Add(PInvoke.cphvb_opcode.CPHVB_SQRT, "Sqrt");
            _opcode_func_name.Add(PInvoke.cphvb_opcode.CPHVB_EXP, "Exp");
            _opcode_func_name.Add(PInvoke.cphvb_opcode.CPHVB_NEGATIVE, "Negate");
            _opcode_func_name.Add(PInvoke.cphvb_opcode.CPHVB_LOG, "Log");
            _opcode_func_name.Add(PInvoke.cphvb_opcode.CPHVB_LOG10, "Log10");
            _opcode_func_name.Add(PInvoke.cphvb_opcode.CPHVB_POWER, "Pow");
        }

        /// <summary>
        /// Helper function to get the opcode mapping table for the current type
        /// </summary>
        /// <returns>A mapping between the type used for this executor and the cphVB opcodes</returns>
        public static Dictionary<Type, PInvoke.cphvb_opcode> CreateOpCodeMap<T>()
        {
            Dictionary<Type, PInvoke.cphvb_opcode> res = new Dictionary<Type, PInvoke.cphvb_opcode>();

            Type basic = typeof(NumCIL.Generic.NdArray<>);

            if (typeof(T) == typeof(sbyte))
                basic = typeof(NumCIL.Int8.NdArray);
            else if (typeof(T) == typeof(short))
                basic = typeof(NumCIL.Int16.NdArray);
            else if (typeof(T) == typeof(int))
                basic = typeof(NumCIL.Int32.NdArray);
            else if (typeof(T) == typeof(long))
                basic = typeof(NumCIL.Int64.NdArray);
            else if (typeof(T) == typeof(byte))
                basic = typeof(NumCIL.UInt8.NdArray);
            else if (typeof(T) == typeof(ushort))
                basic = typeof(NumCIL.UInt16.NdArray);
            else if (typeof(T) == typeof(uint))
                basic = typeof(NumCIL.UInt32.NdArray);
            else if (typeof(T) == typeof(ulong))
                basic = typeof(NumCIL.UInt64.NdArray);
            else if (typeof(T) == typeof(float))
                basic = typeof(NumCIL.Float.NdArray);
            else if (typeof(T) == typeof(double))
                basic = typeof(NumCIL.Double.NdArray);
            else
                throw new Exception("Unexpected type: " + (typeof(T)).FullName);

            foreach (var e in _opcode_func_name)
            {
                try { res[basic.Assembly.GetType(basic.Namespace + "." + e.Value)] = e.Key; }
                catch { }
            }

            res[typeof(NumCIL.CopyOp<T>)] = PInvoke.cphvb_opcode.CPHVB_IDENTITY;
            res[typeof(NumCIL.GenerateOp<T>)] = PInvoke.cphvb_opcode.CPHVB_IDENTITY;
            if (VEM.Instance.SupportsRandom)
                res[typeof(NumCIL.Generic.RandomGeneratorOp<T>)] = PInvoke.cphvb_opcode.CPHVB_USERFUNC;
            if (VEM.Instance.SupportsReduce)
                res[typeof(NumCIL.UFunc.LazyReduceOperation<T>)] = PInvoke.cphvb_opcode.CPHVB_USERFUNC;

            return res;
        }
    }

    public class PendingOpCounter<T> : PendingOperation<T>, IDisposable
    {
        private static long _pendingOpCount = 0;
        public static long PendingOpCount { get { return _pendingOpCount; } }
        private bool m_isDisposed = false;

        public PendingOpCounter(IOp<T> operation, params NdArray<T>[] operands)
            : base(operation, operands)
        {
            System.Threading.Interlocked.Increment(ref _pendingOpCount);
        }

        protected void Dispose(bool disposing)
        {
            if (!m_isDisposed)
            {
                System.Threading.Interlocked.Decrement(ref _pendingOpCount);
                m_isDisposed = true;

                if (disposing)
                    GC.SuppressFinalize(this);
            }
        }

        public void Dispose()
        {
            Dispose(true);
        }

        ~PendingOpCounter()
        {
            Dispose(false);
        }
    }

    /// <summary>
    /// Basic accessor for a cphVB array
    /// </summary>
    /// <typeparam name="T">The type of data kept in the underlying array</typeparam>
    public class cphVBAccessor<T> : NumCIL.Generic.LazyAccessor<T>, IDisposable
    {
        /// <summary>
        /// Instance of the VEM that is used
        /// </summary>
        protected static VEM VEM = NumCIL.cphVB.VEM.Instance;

        /// <summary>
        /// The maximum number of instructions to queue
        /// </summary>
        protected static readonly long HIGH_WATER_MARK = 2000;

        /// <summary>
        /// Local copy of the type, to avoid lookups in the VEM dictionary
        /// </summary>
        protected static readonly PInvoke.cphvb_type CPHVB_TYPE = VEM.MapType(typeof(T));

        /// <summary>
        /// A lookup table that maps NumCIL operation types to cphVB opcodes
        /// </summary>
        protected static Dictionary<Type, PInvoke.cphvb_opcode> OpcodeMap = OpCodeMapper.CreateOpCodeMap<T>();

        /// <summary>
        /// Constructs a new data accessor for the given size
        /// </summary>
        /// <param name="size">The size of the data</param>
        public cphVBAccessor(long size) : base(size) { }

        /// <summary>
        /// Constructs a new data accessor for a pre-allocated block of storage
        /// </summary>
        /// <param name="data"></param>
        public cphVBAccessor(T[] data) : base(data) { m_ownsData = true; }

        /// <summary>
        /// A pointer to the base-array view structure
        /// </summary>
        protected PInvoke.cphvb_array_ptr m_externalData = PInvoke.cphvb_array_ptr.Null;

        /// <summary>
        /// A value indicating if NumCIL owns the data, false means that cphVB owns the data
        /// </summary>
        protected bool m_ownsData = false;

        /// <summary>
        /// A pointer to internally allocated data which is pinned
        /// </summary>
        protected GCHandle m_handle;

        /// <summary>
        /// Returns the data block, flushed and updated
        /// </summary>
        public override T[] Data
        {
            get
            {
                MakeDataManaged();
                return base.Data;
            }
        }

        public override void AddOperation(IOp<T> operation, params NdArray<T>[] operands)
        {
            lock (Lock)
                PendingOperations.Add(new PendingOpCounter<T>(operation, operands));

            if (PendingOpCounter<T>.PendingOpCount > HIGH_WATER_MARK)
            {
                this.Flush();
            }
        }

        /// <summary>
        /// Pins the allocated data and returns the pinned pointer
        /// </summary>
        /// <returns>A pinned pointer</returns>
        internal virtual PInvoke.cphvb_array_ptr Pin()
        {
            if (m_data == null && m_externalData == PInvoke.cphvb_array_ptr.Null)
            {
                //Data is not yet allocated, convert to external storage
                m_externalData = VEM.CreateArray(CPHVB_TYPE, m_size);
                m_ownsData = false;
            }
            else if (m_externalData == PInvoke.cphvb_array_ptr.Null)
            {
                //Internally allocated data, we need to pin it
                if (!m_handle.IsAllocated)
                    m_handle = GCHandle.Alloc(m_data, GCHandleType.Pinned);

                m_externalData = VEM.CreateArray(CPHVB_TYPE, m_size);
                m_externalData.Data = m_handle.AddrOfPinnedObject();
                m_ownsData = true;
            }
            else if (m_ownsData && m_externalData.Data == IntPtr.Zero)
            {
                //Internally allocated data, we need to pin it
                if (!m_handle.IsAllocated)
                    m_handle = GCHandle.Alloc(m_data, GCHandleType.Pinned);
                m_externalData.Data = m_handle.AddrOfPinnedObject();
            }

            return m_externalData;
        }

        /// <summary>
        /// Unpins allocated data
        /// </summary>
        protected void Unpin()
        {
            if (m_externalData != PInvoke.cphvb_array_ptr.Null)
                VEM.Execute(new PInvoke.cphvb_instruction(PInvoke.cphvb_opcode.CPHVB_SYNC, m_externalData));

            if (m_handle.IsAllocated)
            {
                m_handle.Free();

                m_externalData.Data = IntPtr.Zero;
            }
        }

        /// <summary>
        /// Makes the data managed
        /// </summary>
        protected void MakeDataManaged()
        {
            if (m_size < 0)
                throw new ObjectDisposedException(this.GetType().FullName);

            if (m_ownsData && m_externalData != PInvoke.cphvb_array_ptr.Null)
            {
                this.Unpin();
                m_externalData.Data = IntPtr.Zero;
                return;
            }

            //Allocate data internally, and flush instructions as required
            T[] data = base.Data;

            //If data is allocated in cphVB, we need to flush it and de-allocate it
            if (m_externalData != PInvoke.cphvb_array_ptr.Null && !m_ownsData)
            {
                this.Unpin();

                //TODO: Figure out if we can avoid the copy by having a pinvoke method that returns a float[]

                IntPtr actualData = m_externalData.Data;
                if (actualData == IntPtr.Zero)
                {
                    //The array is "empty" which will be zeroes in NumCIL
                }
                else
                {
                    if (m_size > int.MaxValue)
                        throw new OverflowException();

                    //Then copy the data into the local buffer
                    if (typeof(T) == typeof(float))
                        Marshal.Copy(actualData, (float[])(object)data, 0, (int)m_size);
                    else if (typeof(T) == typeof(double))
                        Marshal.Copy(actualData, (double[])(object)data, 0, (int)m_size);
                    else if (typeof(T) == typeof(sbyte))
                    {
                        //TODO: Probably faster to just call memcpy in native code
                        sbyte[] xref = (sbyte[])(object)data;
                        int sbytesize = Marshal.SizeOf(typeof(sbyte));

                        if (m_size > int.MaxValue)
                        {
                            IntPtr xptr = actualData;
                            for (long i = 0; i < m_size; i++)
                            {
                                xref[i] = (sbyte)Marshal.ReadByte(xptr);
                                xptr = IntPtr.Add(xptr, sbytesize);
                            }
                        }
                        else
                        {
                            for (int i = 0; i < m_size; i++)
                                xref[i] = (sbyte)Marshal.ReadByte(actualData);
                        }
                    }
                    else if (typeof(T) == typeof(short))
                        Marshal.Copy(actualData, (short[])(object)data, 0, (int)m_size);
                    else if (typeof(T) == typeof(int))
                        Marshal.Copy(actualData, (int[])(object)data, 0, (int)m_size);
                    else if (typeof(T) == typeof(long))
                        Marshal.Copy(actualData, (long[])(object)data, 0, (int)m_size);
                    else if (typeof(T) == typeof(byte))
                        Marshal.Copy(actualData, (byte[])(object)data, 0, (int)m_size);
                    else if (typeof(T) == typeof(ushort))
                    {
                        //TODO: Probably faster to just call memcpy in native code
                        ushort[] xref = (ushort[])(object)data;
                        int ushortsize = Marshal.SizeOf(typeof(ushort));

                        if (m_size > int.MaxValue)
                        {
                            IntPtr xptr = actualData;
                            for (long i = 0; i < m_size; i++)
                            {
                                xref[i] = (ushort)Marshal.ReadInt16(xptr);
                                xptr = IntPtr.Add(xptr, ushortsize);
                            }
                        }
                        else
                        {
                            for (int i = 0; i < m_size; i++)
                                xref[i] = (ushort)Marshal.ReadInt16(actualData);
                        }
                    }
                    else if (typeof(T) == typeof(uint))
                    {
                        //TODO: Probably faster to just call memcpy in native code
                        uint[] xref = (uint[])(object)data;
                        int uintsize = Marshal.SizeOf(typeof(uint));

                        if (m_size > int.MaxValue)
                        {
                            IntPtr xptr = actualData;
                            for (long i = 0; i < m_size; i++)
                            {
                                xref[i] = (uint)Marshal.ReadInt32(xptr);
                                xptr = IntPtr.Add(xptr, uintsize);
                            }
                        }
                        else
                        {
                            for (int i = 0; i < m_size; i++)
                                xref[i] = (uint)Marshal.ReadInt32(actualData);
                        }
                    }
                    else if (typeof(T) == typeof(ulong))
                    {
                        //TODO: Probably faster to just call memcpy in native code
                        ulong[] xref = (ulong[])(object)data;
                        int ulongsize = Marshal.SizeOf(typeof(ulong));

                        if (m_size > int.MaxValue)
                        {
                            IntPtr xptr = actualData;
                            for (long i = 0; i < m_size; i++)
                            {
                                xref[i] = (ulong)Marshal.ReadInt64(xptr);
                                xptr = IntPtr.Add(xptr, ulongsize);
                            }
                        }
                        else
                        {
                            for (int i = 0; i < m_size; i++)
                                xref[i] = (ulong)Marshal.ReadInt64(actualData);
                        }
                    }
                    else
                        throw new cphVBException(string.Format("Unexpected data type: {0}", typeof(T).FullName));
                }


                //Release the unmanaged copy
                VEM.Execute(new PInvoke.cphvb_instruction(PInvoke.cphvb_opcode.CPHVB_DESTROY, m_externalData));
                m_externalData = PInvoke.cphvb_array_ptr.Null;
                m_ownsData = true;
            }
        }


        /// <summary>
        /// Executes all pending operations in the list
        /// </summary>
        /// <param name="work">The list of operations to execute</param>
        public override void ExecuteOperations(IEnumerable<PendingOperation<T>> work)
        {
            List<PendingOperation<T>> unsupported = new List<PendingOperation<T>>();
            List<IInstruction> supported = new List<IInstruction>();
            //This list keeps all scalars so the GC does not collect them
            // before the instructions are executed, remove list once
            // constants are supported in score/mcore

            List<NdArray<T>> scalars = new List<NdArray<T>>();
            long i = 0;

            foreach (var op in work)
            {
                Type t;
                bool isScalar;
                IOp<T> ops = op.Operation;
                NdArray<T>[] operands = op.Operands;
                i++;

                if (ops is IScalarAccess<T>)
                {
                    t = ((IScalarAccess<T>)ops).Operation.GetType();
                    isScalar = true;
                }
                else
                {
                    t = op.Operation.GetType();
                    isScalar = false;
                }

                PInvoke.cphvb_opcode opcode;
                if (OpcodeMap.TryGetValue(t, out opcode))
                {
                    if (unsupported.Count > 0)
                    {
                        base.ExecuteOperations(unsupported);
                        unsupported.Clear();
                    }

                    if (isScalar)
                    {
                        IScalarAccess<T> sa = (IScalarAccess<T>)ops;

                        //Change to true once score supports constants
                        if (false)
                        {
                            if (sa.Operation is IBinaryOp<T>)
                                supported.Add(VEM.CreateInstruction<T>(CPHVB_TYPE, opcode, operands[0], operands[1], new PInvoke.cphvb_constant(CPHVB_TYPE, sa.Value)));
                            else
                                supported.Add(VEM.CreateInstruction<T>(CPHVB_TYPE, opcode, operands[0], new PInvoke.cphvb_constant(CPHVB_TYPE, sa.Value)));
                        }
                        else
                        {
                            //Since we have no constant support, we mimic the constant with a 1D array
                            var scalarAcc = new cphVBAccessor<T>(1);
                            scalarAcc.Data[0] = sa.Value;
                            NdArray<T> scalarOp;

                            if (sa.Operation is IBinaryOp<T>)
                            {
                                Shape bShape = Shape.ToBroadcastShapes(operands[1].Shape, new Shape(1)).Item2;
                                scalarOp = new NdArray<T>(scalarAcc, bShape);

                                supported.Add(VEM.CreateInstruction<T>(CPHVB_TYPE, opcode, operands[0], operands[1], scalarOp));
                            }
                            else
                            {
                                Shape bShape = Shape.ToBroadcastShapes(operands[0].Shape, new Shape(1)).Item2;
                                scalarOp = new NdArray<T>(scalarAcc, bShape);
                                supported.Add(VEM.CreateInstruction<T>(CPHVB_TYPE, opcode, operands[0], scalarOp));
                            }

                            scalars.Add(scalarOp);
                        }
                    }
                    else
                    {
                        bool isSupported = true;

                        if (opcode == PInvoke.cphvb_opcode.CPHVB_USERFUNC)
                        {
                            if (VEM.SupportsRandom && ops is NumCIL.Generic.RandomGeneratorOp<T>)
                            {
                                supported.Add(VEM.CreateRandomInstruction<T>(CPHVB_TYPE, operands[0]));
                                isSupported = true;
                            }
                            else if (VEM.SupportsReduce && ops is NumCIL.UFunc.LazyReduceOperation<T>)
                            {
                                NumCIL.UFunc.LazyReduceOperation<T> lzop = (NumCIL.UFunc.LazyReduceOperation<T>)op.Operation;
                                PInvoke.cphvb_opcode rop;
                                if (OpcodeMap.TryGetValue(lzop.Operation.GetType(), out rop))
                                {
                                    supported.Add(VEM.CreateReduceInstruction<T>(CPHVB_TYPE, rop, lzop.Axis, operands[0], operands[1]));
                                    isSupported = true;
                                }
                            }


                            if (!isSupported)
                            {
                                if (supported.Count > 0)
                                {
                                    VEM.Execute(supported);
                                    supported.Clear();
                                }

                                unsupported.Add(op);
                            }
                        }
                        else
                        {
                            if (operands.Length == 1)
                                supported.Add(VEM.CreateInstruction<T>(CPHVB_TYPE, opcode, operands[0]));
                            else if (operands.Length == 2)
                                supported.Add(VEM.CreateInstruction<T>(CPHVB_TYPE, opcode, operands[0], operands[1]));
                            else if (operands.Length == 3)
                                supported.Add(VEM.CreateInstruction<T>(CPHVB_TYPE, opcode, operands[0], operands[1], operands[2]));
                            else
                                supported.Add(VEM.CreateInstruction<T>(CPHVB_TYPE, opcode, operands));
                        }
                    }
                }
                else
                {
                    if (supported.Count > 0)
                    {
                        VEM.Execute(supported);
                        supported.Clear();
                    }

                    unsupported.Add(op);
                }
            }

            if (supported.Count > 0 && unsupported.Count > 0)
                throw new InvalidOperationException("Unexpected result, both supported and non-supported operations");

            if (unsupported.Count > 0)
                base.ExecuteOperations(unsupported);

            if (supported.Count > 0)
                VEM.Execute(supported);
            //TODO: Do we want to do it now, or just let the GC figure it out?
            foreach (var op in work)
                if (op is IDisposable)
                    ((IDisposable)op).Dispose();

            //Touch the data to prevent the scalars from being GC'ed
            //Remove once constants are supported
            foreach (var op in scalars)
                op.Name = null;
        }

        /// <summary>
        /// Releases all held resources
        /// </summary>
        /// <param name="disposing">True if called from the Dispose method, false if invoked from the finalizer</param>
        protected virtual void Dispose(bool disposing)
        {
            if (m_size > 0)
            {
                if (m_handle.IsAllocated)
                {
                    m_handle.Free();

                    if (m_externalData != PInvoke.cphvb_array_ptr.Null)
                        PInvoke.cphvb_data_set(m_externalData, IntPtr.Zero);
                }

                if (m_externalData != PInvoke.cphvb_array_ptr.Null)
                {
                    VEM.ExecuteRelease(new PInvoke.cphvb_instruction(PInvoke.cphvb_opcode.CPHVB_DESTROY, m_externalData));
                    m_externalData = PInvoke.cphvb_array_ptr.Null;
                }

                m_data = null;
                m_size = -1;

                if (disposing)
                    GC.SuppressFinalize(this);
            }

        }

        /// <summary>
        /// Releases all held resources
        /// </summary>
        public void Dispose()
        {
            Dispose(true);
        }

        /// <summary>
        /// Destructor for non-disposed elements
        /// </summary>
        ~cphVBAccessor()
        {
            Dispose(false);
        }
    }
}
