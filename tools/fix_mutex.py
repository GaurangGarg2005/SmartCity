with open('backend/src/sim/simulator.h', 'rb') as f:
    data = f.read()

data = data.replace(b'mutable std::mutex mutex_;', b'mutable std::recursive_mutex mutex_;')
with open('backend/src/sim/simulator.h', 'wb') as f:
    f.write(data)

with open('backend/src/sim/simulator.cpp', 'rb') as f:
    cpp_data = f.read()

cpp_data = cpp_data.replace(b'std::lock_guard<std::mutex>', b'std::lock_guard<std::recursive_mutex>')
with open('backend/src/sim/simulator.cpp', 'wb') as f:
    f.write(cpp_data)

print('Updated simulator.h and simulator.cpp to use std::recursive_mutex')

