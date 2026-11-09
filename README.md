# WhipCPU

WhipCPU là ứng dụng desktop Windows viết bằng C++20, Win32 và GDI+. Nhấp chuột trái vào một ứng dụng để tung roi hoạt hình và tạm nâng priority của tiến trình foreground từ `NORMAL` lên `ABOVE_NORMAL`.

## Build và chạy

Cần Windows 10 phiên bản 1703 trở lên hoặc Windows 11, Visual Studio 2022 với workload **Desktop development with C++**, Windows SDK và CMake 3.21 trở lên. Chạy các lệnh sau trong PowerShell tại thư mục dự án.

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
.\build\Release\WhipCPU.exe
```

Icon và tiếng roi được nhúng vào EXE. Chạy với quyền người dùng thường. Khi chạy, biểu tượng WhipCPU nằm ở vùng thông báo của taskbar, có thể nằm trong nhóm biểu tượng ẩn.

## Sử dụng

- **Whip Mode** bật hoặc tắt cả việc boost và hiệu ứng. Khi tắt, WhipCPU khôi phục priority đã đổi.
- **Animation**, **Sound** và **Animation Intensity** điều khiển hiệu ứng riêng. Mặc định bật Whip Mode và Animation, chọn mức Normal và tắt âm thanh.
- **Reduced Motion** chỉ hiện nhãn kết quả tĩnh. Giá trị ban đầu theo cài đặt hiệu ứng của Windows.
- Tooltip và menu hiện tên EXE và PID mà WhipCPU đang quản lý. Mở menu có thể chuyển foreground, vì vậy mục trạng thái trong menu ghi nhận thời điểm ngay trước khi mở.
- **Restore All Priorities** khôi phục priority đang quản lý. **Exit** thực hiện khôi phục trước khi thoát. Các tuỳ chọn khởi tạo lại khi mở chương trình.

Roi hiển thị cạnh con trỏ và đánh vào vị trí nhấp trong khoảng 500 ms. Overlay cho phép nhấp xuyên qua và giữ nguyên focus bàn phím. Khi chuột đứng yên và hiệu ứng đã kết thúc, ứng dụng dừng vẽ lại.

WhipCPU quản lý tối đa một tiến trình tại một thời điểm. Chuyển foreground bằng bàn phím cũng khôi phục priority cũ. Ứng dụng mới chỉ được boost khi người dùng nhấp vào nó. WhipCPU bỏ qua tiến trình hệ thống quan trọng, Windows Shell, một số tiến trình host dùng chung và tiến trình có priority khác `NORMAL`. Khi Windows từ chối, hiệu ứng hiện **PRIORITY DENIED**. Nếu Windows từ chối khôi phục, WhipCPU giữ handle, thử lại và tạm ngừng boost mới.

Priority điều chỉnh cách Windows lập lịch CPU. Thời gian xử lý thực tế phụ thuộc vào tải hệ thống và cách ứng dụng phân chia công việc. WhipCPU chỉ đổi priority của tiến trình sở hữu cửa sổ foreground. [Microsoft Learn về SetPriorityClass](https://learn.microsoft.com/windows/win32/api/processthreadsapi/nf-processthreadsapi-setpriorityclass)

## Kiểm tra thủ công

1. Mở hai ứng dụng desktop chạy với priority Normal. Lấy PID từ tab **Details** của Task Manager rồi chạy đoạn PowerShell dưới đây với hai PID đó. Đặt cửa sổ PowerShell cạnh hai ứng dụng để theo dõi priority mà vẫn giữ ứng dụng cần thử ở foreground.
2. Nhấp vào ứng dụng thứ nhất. Kiểm tra hiệu ứng **CHÁT!**, nhãn **+CPU PRIORITY**, thao tác nhấp vẫn đến ứng dụng và priority chuyển sang **Above normal**. Nhấp lần nữa để kiểm tra **ALREADY BOOSTED**.
3. Nhấp vào ứng dụng thứ hai. Kiểm tra tiến trình thứ nhất về **Normal**, tiến trình thứ hai lên **Above normal**. Dùng Alt+Tab và xác nhận priority của tiến trình vừa rời khỏi được khôi phục.
4. Thử các mục trong khay hệ thống và xác nhận **Whip Mode** tắt, **Restore All Priorities** và **Exit** đều khôi phục priority. Thử một ứng dụng chạy elevated từ WhipCPU chạy thường và kiểm tra phản hồi nếu Windows từ chối thao tác.
5. Di chuột và nhấp ở mép màn hình trên hai màn hình có mức scale khác nhau. Kiểm tra roi bám con trỏ, điểm va chạm khớp vị trí nhấp và overlay giữ đúng kích thước theo DPI.

```powershell
$whipTestPids = 1234, 5678
while ($true) {
    Clear-Host
    Get-Process -Id $whipTestPids | Format-Table Id, ProcessName, PriorityClass
    Start-Sleep -Milliseconds 300
}
```
