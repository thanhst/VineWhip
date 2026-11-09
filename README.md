# VineWhip

VineWhip là cửa sổ desktop nhỏ viết bằng C++20, Win32 và GDI+. Mỗi hàng đại diện cho một tiến trình có cửa sổ ứng dụng đang mở trên desktop hiện tại, kể cả cửa sổ thu nhỏ. Nhân vật ngồi gõ phím và dùng icon của ứng dụng làm mặt. Danh sách tự cập nhật và có thể cuộn.

## Build và chạy

Cần Windows 10 phiên bản 1703 trở lên hoặc Windows 11, Visual Studio 2022 với workload **Desktop development with C++**, Windows SDK và CMake 3.21 trở lên. Chạy các lệnh sau trong PowerShell tại thư mục dự án.

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
.\build\Release\WhipCPU.exe
```

Icon và tiếng roi được nhúng vào EXE. Chạy với quyền người dùng thường. `WhipCPU.exe` mở cửa sổ **VineWhip** và thêm một biểu tượng vào vùng thông báo của taskbar.

## Sử dụng

- Nhấp vào một nhân vật trong VineWhip để quất roi. Bạn cũng có thể chọn bằng phím mũi tên rồi bấm **Enter**. Khi Windows xác nhận priority đã lên **Above normal**, nhân vật gõ nhanh hơn và hàng đó đổi sang màu xanh.
- Di chuột ra ngoài VineWhip để trở lại con trỏ thường. Boost vẫn được giữ khi chuyển focus hay thu nhỏ VineWhip. Quất một nhân vật khác sẽ khôi phục priority của tiến trình trước rồi thử boost tiến trình mới.
- **Ctrl + Alt + B** hoặc nút **BOOST ON/OFF** trong cửa sổ bật hoặc tắt riêng CPU Boost. Tắt sẽ khôi phục priority đang quản lý. Khi bật lại, cú quất tiếp theo mới kích hoạt boost.
- Nhấp phải trong cửa sổ hoặc vào biểu tượng ở khay hệ thống để mở tuỳ chọn. **Whip Mode** điều khiển cả roi và boost, còn **Animation**, **Sound** và **Animation Intensity** điều khiển hiệu ứng riêng. Mặc định bật Whip Mode, CPU Boost và Animation, chọn mức Normal và tắt âm thanh.
- **Reduced Motion** chỉ hiện nhãn kết quả tĩnh. Giá trị ban đầu theo cài đặt hiệu ứng của Windows.
- **Restore All Priorities** khôi phục priority đang quản lý. Bấm **X** hoặc chọn **Exit** để khôi phục priority và thoát hẳn. Các tuỳ chọn khởi tạo lại khi mở chương trình.

VineWhip quản lý tối đa một tiến trình tại một thời điểm. Tiến trình hệ thống quan trọng, Windows Shell, host dùng chung và tiến trình có priority khác `NORMAL` được bỏ qua. Khi Windows từ chối boost, hàng hiện **PRIORITY DENIED**. Nếu Windows từ chối khôi phục, VineWhip giữ handle, thử lại và tạm dừng boost mới.

Priority điều chỉnh cách Windows lập lịch CPU cho tiến trình được chọn. Thời gian xử lý thực tế còn phụ thuộc vào tải hệ thống và cách ứng dụng phân chia công việc. [Microsoft Learn về SetPriorityClass](https://learn.microsoft.com/windows/win32/api/processthreadsapi/nf-processthreadsapi-setpriorityclass)

## Kiểm tra thủ công

1. Mở hai ứng dụng desktop chạy với priority Normal. Lấy PID từ tab **Details** của Task Manager rồi chạy đoạn PowerShell dưới đây với hai PID đó. Đặt cửa sổ PowerShell cạnh hai ứng dụng để theo dõi priority mà vẫn giữ ứng dụng cần thử ở foreground.
2. Trong VineWhip, nhấp hàng của ứng dụng thứ nhất. Kiểm tra hiệu ứng **CHÁT!**, nhịp gõ nhanh hơn và priority chuyển sang **Above normal**.
3. Nhấp trực tiếp vào ứng dụng thứ hai bên ngoài VineWhip hoặc dùng Alt+Tab. Xác nhận chuột trở lại bình thường và boost vẫn thuộc về tiến trình thứ nhất. Quay lại VineWhip và quất hàng thứ hai để chuyển boost.
4. Nhấn **Ctrl + Alt + B** và kiểm tra tiến trình thứ hai trở về **Normal**. Quất tiếp để thấy **BOOST OFF**. Bật lại boost và thử đóng VineWhip bằng **X** sau khi boost để kiểm tra khôi phục priority.
5. Mở đủ ứng dụng để cuộn danh sách. Thử mở, thu nhỏ và đóng cửa sổ ứng dụng. Kiểm tra danh sách cập nhật và kích thước VineWhip trên màn hình có mức scale khác nhau.

```powershell
$whipTestPids = 1234, 5678
while ($true) {
    Clear-Host
    Get-Process -Id $whipTestPids | Format-Table Id, ProcessName, PriorityClass
    Start-Sleep -Milliseconds 300
}
```
