# esp-idf-gh-actions-demo

最小 ESP-IDF 示例项目，使用 GitHub Actions 云端编译，无需本地安装 ESP-IDF。

## 使用方法

1. 在 GitHub 上新建一个仓库，把本目录内容推上去：

   ```bash
   git init
   git add .
   git commit -m "feat: ESP-IDF GitHub Actions demo"
   git branch -M main
   git remote add origin https://github.com/<你的用户名>/<仓库名>.git
   git push -u origin main
   ```

2. 推送后进入仓库的 **Actions** 页面，工作流 `Build ESP-IDF` 会自动开始编译。

3. 编译完成后，在对应运行记录的 **Artifacts** 里下载 `firmware-esp32`（含 .bin/.elf 等）。

## 调整配置

- **ESP-IDF 版本**：改 `.github/workflows/build.yml` 顶部的 `IDF_VERSION`。
- **目标芯片**：改 `matrix.target`（如 `esp32s3`、`esp32c3`），并同时改 `sdkconfig.defaults` 里的 `CONFIG_IDF_TARGET`。

工作流使用官方动作 `espressif/esp-idf-ci-action@v1`，在 GitHub 托管的 Ubuntu 运行器上执行 `idf.py build`。
