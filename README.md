# Multi ECA App

VS + Qt Tools upper-computer software for multi-channel array eddy current
data acquisition, display, signal processing, and imaging.

## Local Workspace

Original local path:

`E:\yu\VSQt\multi_eca_app`

## Main Project

- Solution: `multi_eca_app.sln`
- Visual Studio project: `multi_eca_app/multi_eca_app.vcxproj`
- Main UI: `multi_eca_app/multi_eca_app.ui`
- Main source files:
  - `multi_eca_app/multi_eca_app.cpp`
  - `multi_eca_app/SerialWorker.cpp`
  - `multi_eca_app/DataProcessWorker.cpp`
  - `multi_eca_app/ImagingWorker.cpp`
  - `multi_eca_app/ImagingView.cpp`
  - `multi_eca_app/ChartPanel.cpp`
  - `multi_eca_app/ChartsController.cpp`

## External Dependency Note

The current `.vcxproj` references QCustomPlot through an external local path:

`..\..\arry_current\CustomPlot\QCustomPlot.tar\QCustomPlot\qcustomplot.cpp`

and:

`..\..\arry_current\CustomPlot\QCustomPlot.tar\QCustomPlot\qcustomplot.h`

This repository preserves the current project structure. If the project needs to
be built on another machine, either keep the same relative dependency layout or
vendor QCustomPlot into this repository and update the `.vcxproj` references.

## Git Policy

The repository tracks source code and project files only. The following are
kept locally and intentionally ignored:

- Visual Studio local state: `.vs/`
- build outputs: `x64/`, `Debug/`, `Release/`
- packaged deployments: `dist/`, `app1.1/`
- zip packages
- executable/DLL/debug/object files
- local rawstream sample data under `daochy/`

This keeps the GitHub repository lightweight and useful for code review and
incremental development.

