# 纯热成像 Web 归档

该目录是可见光版 Web 改造前保留的纯热成像上位机快照。

- 独立入口：`thermal_web_thermal_only/app.py`
- 页面资源：`thermal_web_thermal_only/static/`
- 继续复用项目公共串口解析器：`tools/uart_temp14_parser.py`
- 启动后自动创建自己的 `downloads/`，不会与 `thermal_web/downloads/` 混用
- 后续可见光页面继续在 `thermal_web/` 中开发，不要把本目录同步覆盖

启动命令：

```powershell
& 'C:\Users\26218\AppData\Local\Programs\Python\Python310\python.exe' thermal_web_thermal_only/app.py
```

浏览器地址：`http://127.0.0.1:8000`
