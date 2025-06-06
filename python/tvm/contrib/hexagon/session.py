# Licensed to the Apache Software Foundation (ASF) under one
# or more contributor license agreements.  See the NOTICE file
# distributed with this work for additional information
# regarding copyright ownership.  The ASF licenses this file
# to you under the Apache License, Version 2.0 (the
# "License"); you may not use this file except in compliance
# with the License.  You may obtain a copy of the License at
#
#   http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing,
# software distributed under the License is distributed on an
# "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
# KIND, either express or implied.  See the License for the
# specific language governing permissions and limitations
# under the License.
# pylint: disable=consider-using-from-import

"""Defines a Session class for Hexagon devices."""

import os
import pathlib
import tempfile
from typing import Union

import tvm
import tvm.contrib.hexagon as hexagon
from tvm import rpc as _rpc
from tvm import runtime
from tvm.contrib import utils

from .tools import HEXAGON_SIMULATOR_NAME, export_module


class Session:
    """Hexagon Device Session

    Parameters
    ----------
    remote_workspace : Union[str, pathlib.Path]
        Remote workspace path

    rpc_tracker : tuple(str, int)
        RPC tracker host and port number.

    rpc_server_key : str
        RPC server key on remote device.

    serial_number : str
        Device serial number. `simulator` used for hexagon simulator.

    session_name : str
        Hexagon RPC session name.

    remote_stack_size_bytes : int
        The stack size of the remote device, to be passed to
        tvm.contrib.hexagon.create_hexagon_session.

    rpc_receive_buffer_size_bytes : int
        RPC receive buffer size in bytes.
    """

    def __init__(
        self,
        remote_workspace: Union[str, pathlib.Path],
        rpc_tracker: tuple,
        rpc_server_key: str,
        serial_number: str,
        session_name: str = "hexagon-rpc",
        remote_stack_size_bytes: int = 256 * 1024,  # Min size for main thread in QuRT/sim
        rpc_receive_buffer_size_bytes: int = 256 * 1024 * 1024,  # Size for passing hexagon tests
    ):
        self._workspace = str(remote_workspace)
        self._rpc_tracker = rpc_tracker
        self._rpc_server_key = rpc_server_key
        self._serial_number = serial_number
        self._session_name: str = session_name
        self._remote_stack_size_bytes: int = remote_stack_size_bytes
        self._rpc_receive_buffer_size_bytes: int = rpc_receive_buffer_size_bytes
        self._rpc = None
        self._requires_cpu_device = False
        self._device = None

    def __enter__(self):
        if self._rpc:
            # Already initialized
            return self

        tracker = _rpc.connect_tracker(self._rpc_tracker[0], self._rpc_tracker[1])
        try:
            self._rpc = tracker.request(
                self._rpc_server_key,
                priority=0,
                session_timeout=0,
                session_constructor_args=[
                    "tvm.contrib.hexagon.create_hexagon_session",
                    self._session_name,
                    self._remote_stack_size_bytes,
                    os.environ.get("HEXAGON_SIM_ARGS", ""),
                    self._rpc_receive_buffer_size_bytes,
                ],
            )
            func = self._rpc.get_function("device_api.hexagon.acquire_resources")
            func()
            return self

        except RuntimeError as exception:
            raise exception

    def __exit__(self, exc_type, exc_value, exc_traceback):
        try:
            func = self._rpc.get_function("device_api.hexagon.release_resources")
            func()
        except RuntimeError as exception:
            print(
                "Exception occurred while calling release_resources() during Session __exit__: ",
                exception,
            )
        finally:
            # close session to the tracker
            shutdown_func = self._rpc._sess.get_function("CloseRPCConnection")
            shutdown_func()
            del self._rpc

    @property
    def device(self):
        """Session device."""

        if self._device is not None:
            return self._device

        if self._requires_cpu_device:
            self._device = self._rpc.cpu(0)
        else:
            self._device = self._rpc.hexagon(0)

        return self._device

    def is_simulator(self):
        return self._serial_number == HEXAGON_SIMULATOR_NAME

    def get_function(self, name):
        return self._rpc.get_function(name)

    def upload(self, local_path: Union[str, pathlib.Path], remote_filename: str) -> pathlib.Path:
        """Upload a local file to the remote workspace.

        Parameters
        ----------
        local_path : str or pathlib.Path
            Path to the local file to be copied.
        remote_filename : str
            Name of the file in the remote workspace.

        Returns
        -------
        pathlib.Path :
            Uploaded file remote path.
        """
        upload_func = self._rpc.get_function("tvm.rpc.server.upload")
        remote_path = f"{self._workspace}/{remote_filename}"
        with open(local_path, mode="rb") as src_f:
            data = bytearray(src_f.read())
        upload_func(remote_path, data)
        return remote_path

    def load_module(self, module: Union[str, pathlib.Path, tvm.runtime.Module]):
        """Load TVM module.

        The session must be established (via __enter__) prior to
        calling this function.

        Parameters
        ----------
        module : Union[str, pathlib.Path, tvm.runtime.Module]

            The module to load.  If `module` is a
            `tvm.runtime.Module`, it will be uploaded to the remote
            session and loaded.

            If the object passed is a string or pathlib.Path, it must
            be a full path in the remote system.

        Returns
        -------
        TVMModule :
            TVM module object.
        """

        assert self._rpc is not None, "Hexagon session must be started using __enter__ prior to use"

        if isinstance(module, tvm.runtime.Module):
            with tempfile.TemporaryDirectory() as temp_dir:
                binary_name = "test_binary.so"
                binary_path = export_module(module, temp_dir, binary_name)
                remote_file_path = self.upload(binary_path, binary_name)
        else:
            remote_file_path = module

        assert isinstance(remote_file_path, (str, pathlib.Path)), "Invalid path type:" + str(
            type(remote_file_path)
        )
        return self._rpc.get_function("tvm.hexagon.load_module")(str(remote_file_path))

    def get_executor_from_factory(
        self, module: Union[runtime.executable, str], hexagon_arch: str = "v68"
    ):
        """Create a local GraphModule which consumes a remote libmod.

        Parameters
        ----------

        module : Union[runtime.Executable, str]

            The module to upload to the remote
            session and load.
        hexagon_arch : str
            The hexagon arch to be used
        """
        if isinstance(module, (runtime.Executable, str)):
            return self._vm_executable_executor(module, hexagon_arch=hexagon_arch)

        raise TypeError(f"Unsupported executor type: {type(module)}")

    def _set_device_type(self, module: Union[str, pathlib.Path]):
        """Set session device type(hexagon, cpu) based on target in module.

        Parameters
        ----------

        module: TVMModule
            TVM module object.
        """
        # for cases when module is a single schedule without target attribute.
        if not hasattr(module, "target"):
            self._requires_cpu_device = False
        else:
            assert len(module.target) == 1
            for target in module.target:
                target_type = str(target).split()[0]

            if target_type == "llvm":
                self._requires_cpu_device = True
            else:
                self._requires_cpu_device = False

    def _vm_executable_executor(
        self, executable: Union[runtime.Executable, str], hexagon_arch: str
    ):
        """Create a local TVM module which consumes a remote vm executable.

        Parameters
        ----------

        executable : runtime.Executable
            The Executable to upload to the remote and load. This will typically be the
            output of `tvm.compile` or the path to an already built and exported shared library
        hexagon_arch : str
            The hexagon arch to be used

        Returns
        -------
        TVMModule :
            TVM module object
        """
        assert self._rpc is not None, "Hexagon session must be started using __enter__ prior to use"

        if isinstance(executable, runtime.Executable):
            temp_dir = utils.tempdir()
            path_exec = temp_dir.relpath("exec.so")

            executable.export_library(
                path_exec,
                fcompile=hexagon.create_aot_shared,
                hexagon_arch=hexagon_arch,
            )

            path = self.upload(path_exec, "exec.so")
        elif isinstance(executable, str):
            path_exec = executable
        else:
            raise TypeError(f"Unsupported executor type: {type(executable)}")

        path = self.upload(path_exec, "exec.so")
        return self._rpc.get_function("tvm.hexagon.load_module")(str(path))

    def get_profile_output(self, mode: str, path: str):
        assert isinstance(mode, str), f"Invalid mode type, {type(mode)} != str"
        assert isinstance(path, str), f"Invalid path type, {type(path)} != str"
        return self._rpc.get_function("tvm.hexagon.get_profile_output")(mode, path)
