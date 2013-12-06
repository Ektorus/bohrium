using System;
using System.Runtime.InteropServices;
using System.Collections.Generic;

namespace NumCIL.Bohrium2
{
    /// <summary>
    /// Utility class that keeps track of all pinned memory allocations,
    /// which is used for non-bohrium tracked arrays
    /// </summary>
    public static class PinnedArrayTracker
    {
        /// <summary>
        /// The lookup table with all pinned items
        /// </summary>
        private static Dictionary<Array, Tuple<IntPtr, GCHandle, IDisposable>> _allocations = new Dictionary<Array, Tuple<IntPtr, GCHandle, IDisposable>>();

        /// <summary>
        /// The lock object that protects access to the pinner
        /// </summary>
        private static object _lock = new object();
    
        /// <summary>
        /// Pins the array.
        /// </summary>
        /// <param name="item">Item.</param>
        /// <typeparam name="T">The 1st type parameter.</typeparam>
        public static IntPtr PinArray<T>(T[] item)
        {
            lock (_lock)
            {
                Tuple<IntPtr, GCHandle, IDisposable> r;
                if (_allocations.TryGetValue(item, out r))
                    return r.Item1;

                var handle = GCHandle.Alloc(item, GCHandleType.Pinned);
                if (typeof(T) == typeof(float))
                {
                    var p = new PInvoke.bh_base_float32_p(handle.AddrOfPinnedObject(), item.Length);
                    r = new Tuple<IntPtr, GCHandle, IDisposable>(
                        p.ToPointer(),
                        handle,
                        p
                    );
                }
                else
                {
                    handle.Free();
                    throw new Exception("Unexpected data type: " + typeof(T).FullName);
                }
                
                _allocations[item] = r;
                return r.Item1;
            }
        }
        
        /// <summary>
        /// Gets a value indicating if there are pinned entries
        /// </summary>
        /// <value><c>true</c> there are pinned entries; otherwise, <c>false</c>.</value>
        public static bool HasEntries { get { return _allocations.Count != 0; } }
        
        /// <summary>
        /// Releases all pinned items
        /// </summary>
        public static void Release()
        {
            lock (_lock)
            {
                // Ensure we have nothing that depends on this
                Utility.Flush();
                
                foreach (var h in _allocations.Values)
                {
                    h.Item3.Dispose();
                    h.Item2.Free();
                }
                
                _allocations.Clear();
            }
        }
    }
}
