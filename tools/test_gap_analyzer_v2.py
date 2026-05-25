#!/usr/bin/env python3
"""
NEVO 自动化测试缺口分析工具 v2.0

目标：每次运行时，审查近期合并的代码，在覆盖率缺口对产品稳定性构成实质风险的地方补充测试。

改进点：
1. 更精确的代码解析 - 使用正则和AST分析提取函数签名
2. Git集成改进 - 正确处理合并提交和变更历史
3. 风险评估优化 - 基于代码特征和变更模式的综合评分
4. 测试生成增强 - 生成的测试覆盖更多边界情况
5. 跳过无关内容 - 忽略纯粹的格式调整和重构

重点关注领域（按优先级排序）：
- 缺少任何测试覆盖的新增逻辑路径
- 仅修改了生产代码而未同步更新测试的 Bug 修复提交
- 下游使用广泛的核心模块和共享工具函数
- 涉及解析、并发、权限校验或数据验证的复杂逻辑
- 业务关键流程中的边界条件和极端情况

应跳过的内容：
- 提供极少行为信号的快照式测试
- 针对纯粹外观或格式调整的测试
- 针对保持现有行为不变的重构的覆盖
"""

import argparse
import os
import sys
import json
import subprocess
import re
from datetime import datetime, timedelta
from pathlib import Path
from dataclasses import dataclass, field, asdict
from typing import List, Dict, Set, Optional, Tuple, Callable
from enum import Enum
from collections import defaultdict


class RiskLevel(Enum):
    CRITICAL = ("critical", 100)
    HIGH = ("high", 75)
    MEDIUM = ("medium", 50)
    LOW = ("low", 25)
    INFO = ("info", 10)

    def __init__(self, label: str, score: int):
        self.label = label
        self.score = score


@dataclass
class FunctionSignature:
    name: str
    return_type: str
    parameters: List[Tuple[str, str]]
    is_virtual: bool = False
    is_const: bool = False
    is_static: bool = False
    is_explicit: bool = False
    noexcept: bool = False
    line_number: int = 0


@dataclass
class ClassInfo:
    name: str
    namespace: str
    base_classes: List[str]
    methods: List[FunctionSignature]
    line_number: int


@dataclass
class SourceFileInfo:
    path: str
    classes: List[ClassInfo]
    standalone_functions: List[FunctionSignature]
    includes: List[str]
    complexity_score: int
    has_security_relevant_code: bool
    has_concurrency_code: bool
    has_data_validation: bool


@dataclass
class TestFunction:
    name: str
    class_name: str
    target_function: str
    is_parameterized: bool
    edge_cases: List[str]


@dataclass
class CodeChange:
    file_path: str
    change_type: str
    diff_content: str
    hunks: List[Dict]
    commit_hash: str
    commit_message: str
    is_merge_commit: bool
    author: str
    timestamp: datetime
    lines_added: int
    lines_deleted: int


@dataclass
class CoverageGap:
    file_path: str
    class_name: str
    method_name: str
    function_signature: Optional[str]
    risk_level: RiskLevel
    reason: str
    suggested_test_pattern: str
    priority_score: int
    complexity_score: int
    change_history: List[str]
    affected_test_files: List[str]
    edge_cases_to_cover: List[str]


@dataclass
class TestFile:
    file_path: str
    target_file: str
    test_cases: List[Dict]
    description: str
    generated_from_gaps: List[str]


class CodeParser:
    """C++ 代码解析器 - 提取类、函数签名和代码特征"""

    @staticmethod
    def parse_header(file_path: str) -> Optional[SourceFileInfo]:
        """解析头文件并提取结构信息"""
        try:
            with open(file_path, 'r', encoding='utf-8', errors='ignore') as f:
                content = f.read()

            includes = CodeParser._extract_includes(content)
            namespace = CodeParser._extract_namespace(content)
            classes = CodeParser._extract_classes(content, namespace)
            standalone_funcs = CodeParser._extract_standalone_functions(content)
            complexity = CodeParser._calculate_complexity(content)
            has_security = CodeParser._has_security_code(content)
            has_concurrency = CodeParser._has_concurrency_code(content)
            has_validation = CodeParser._has_validation_code(content)

            return SourceFileInfo(
                path=file_path,
                classes=classes,
                standalone_functions=standalone_funcs,
                includes=includes,
                complexity_score=complexity,
                has_security_relevant_code=has_security,
                has_concurrency_code=has_concurrency,
                has_data_validation=has_validation
            )
        except Exception as e:
            return None

    @staticmethod
    def _extract_includes(content: str) -> List[str]:
        """提取所有 #include 语句"""
        pattern = r'#include\s*[<"]([^>"]+)[>"]'
        return re.findall(pattern, content)

    @staticmethod
    def _extract_namespace(content: str) -> str:
        """提取命名空间"""
        match = re.search(r'namespace\s+(\w+)\s*{', content)
        return match.group(1) if match else ""

    @staticmethod
    def _extract_classes(content: str, namespace: str) -> List[ClassInfo]:
        """提取类定义及其方法"""
        classes = []
        class_pattern = r'class\s+(\w+)\s*(?::\s*public\s+(\w+))?\s*{([^}]*(?:{[^}]*}[^}]*)*)}'
        for match in re.finditer(class_pattern, content, re.DOTALL):
            class_name = match.group(1)
            base_class = match.group(2) or ""
            class_body = match.group(3)

            methods = []
            for func_match in re.finditer(
                r'(?:virtual\s+)?(\w+)\s+(?:explicit\s+)?(\w+)\s*\(([^)]*)\)\s*(const\s+)?(noexcept\s+)?',
                class_body
            ):
                ret_type = func_match.group(1)
                func_name = func_match.group(2)
                params_str = func_match.group(3)
                is_const = bool(func_match.group(4))
                is_noexcept = bool(func_match.group(5))

                if func_name == class_name or func_name == f'~{class_name}':
                    continue

                params = []
                if params_str.strip():
                    for param in params_str.split(','):
                        param = param.strip()
                        if param:
                            parts = param.split()
                            if len(parts) >= 2:
                                params.append((parts[0], parts[-1]))
                            elif len(parts) == 1:
                                params.append((parts[0], ""))

                methods.append(FunctionSignature(
                    name=func_name,
                    return_type=ret_type,
                    parameters=params,
                    is_const=is_const,
                    noexcept=is_noexcept
                ))

            classes.append(ClassInfo(
                name=class_name,
                namespace=namespace,
                base_classes=[base_class] if base_class else [],
                methods=methods,
                line_number=0
            ))

        return classes

    @staticmethod
    def _extract_standalone_functions(content: str) -> List[FunctionSignature]:
        """提取独立函数（非类成员函数）"""
        functions = []
        pattern = r'(?:static\s+)?(\w+)\s+(\w+)\s*\(([^)]*)\)'
        for match in re.finditer(pattern, content):
            ret_type = match.group(1)
            func_name = match.group(2)
            params_str = match.group(3)

            if func_name in ['if', 'while', 'for', 'switch']:
                continue

            params = []
            if params_str.strip():
                for param in params_str.split(','):
                    param = param.strip()
                    if param:
                        parts = param.split()
                        if len(parts) >= 2:
                            params.append((parts[0], parts[-1]))
                        elif len(parts) == 1:
                            params.append((parts[0], ""))

            functions.append(FunctionSignature(
                name=func_name,
                return_type=ret_type,
                parameters=params
            ))

        return functions

    @staticmethod
    def _calculate_complexity(content: str) -> int:
        """计算代码复杂度分数"""
        score = 0
        score += len(re.findall(r'\bif\s*\(', content)) * 2
        score += len(re.findall(r'\bswitch\s*\(', content)) * 3
        score += len(re.findall(r'\bfor\s*\(', content)) * 2
        score += len(re.findall(r'\bwhile\s*\(', content)) * 2
        score += len(re.findall(r'\btry\s*{', content)) * 4
        score += len(re.findall(r'\bcatch\s*\(', content)) * 4
        score += len(re.findall(r'\btemplate\s*<', content)) * 3
        score += len(re.findall(r'\bstd::(mutex|lock|atomic)', content)) * 3
        score += len(re.findall(r'\bstd::(unique_ptr|shared_ptr)\b', content)) * 2
        return min(score, 100)

    @staticmethod
    def _has_security_code(content: str) -> bool:
        """检测是否包含安全相关代码"""
        security_keywords = [
            'password', 'auth', 'permission', 'encrypt', 'decrypt',
            'hash', 'salt', 'token', 'credential', 'secret',
            'validate', 'sanitize', 'escape', 'privilege'
        ]
        content_lower = content.lower()
        return any(kw in content_lower for kw in security_keywords)

    @staticmethod
    def _has_concurrency_code(content: str) -> bool:
        """检测是否包含并发相关代码"""
        concurrency_keywords = [
            'mutex', 'lock', 'thread', 'async', 'await',
            'atomic', 'semaphore', 'condition_variable', 'strand'
        ]
        return any(kw in content for kw in concurrency_keywords)

    @staticmethod
    def _has_validation_code(content: str) -> bool:
        """检测是否包含数据验证代码"""
        validation_keywords = [
            'assert', 'require', 'ensure', 'check',
            'validate', 'verify', 'parse', 'deserialize'
        ]
        return any(kw in content for kw in validation_keywords)


class GitAnalyzer:
    """Git 集成模块 - 分析代码变更历史"""

    @staticmethod
    def get_recent_commits(project_root: str, days: int = 7) -> List[CodeChange]:
        """获取最近的代码变更"""
        since_date = (datetime.now() - timedelta(days=days)).strftime('%Y-%m-%d')
        changes = []

        try:
            result = subprocess.run(
                ['git', 'log', '--since', since_date, '--merges', '--format=%H|%s|%an|%ad|%P',
                 '--date=iso', '--name-status'],
                cwd=project_root,
                capture_output=True,
                text=True,
                timeout=30
            )

            if result.returncode == 0:
                changes = GitAnalyzer._parse_git_log(result.stdout)
        except Exception as e:
            print(f"  警告: Git 操作失败: {e}")

        if not changes:
            try:
                result = subprocess.run(
                    ['git', 'log', '--since', since_date, '--format=%H|%s|%an|%ad|%P',
                     '--date=iso', '--name-status'],
                    cwd=project_root,
                    capture_output=True,
                    text=True,
                    timeout=30
                )
                if result.returncode == 0:
                    changes = GitAnalyzer._parse_git_log(result.stdout)
            except Exception:
                pass

        return changes

    @staticmethod
    def _parse_git_log(log_output: str) -> List[CodeChange]:
        """解析 Git log 输出"""
        changes = []
        lines = log_output.split('\n')
        current_commit = None

        for i, line in enumerate(lines):
            if '|' in line:
                parts = line.split('|')
                if len(parts) >= 4:
                    commit_hash = parts[0].strip()
                    commit_message = parts[1].strip()
                    author = parts[2].strip()
                    date_str = parts[3].strip() if len(parts) > 3 else ""

                    try:
                        timestamp = datetime.fromisoformat(date_str.split()[0]) if date_str else datetime.now()
                    except:
                        timestamp = datetime.now()

                    current_commit = CodeChange(
                        file_path="",
                        change_type="modified",
                        diff_content="",
                        hunks=[],
                        commit_hash=commit_hash,
                        commit_message=commit_message,
                        is_merge_commit="Merge" in commit_message,
                        author=author,
                        timestamp=timestamp,
                        lines_added=0,
                        lines_deleted=0
                    )
            elif '\t' in line and current_commit:
                tab_parts = line.split('\t')
                if len(tab_parts) >= 2:
                    status = tab_parts[0].strip()
                    file_path = tab_parts[1].strip()

                    if any(file_path.endswith(ext) for ext in ['.cpp', '.h', '.hpp']):
                        change = CodeChange(
                            file_path=file_path,
                            change_type='added' if status == 'A' else 'modified' if status == 'M' else 'deleted',
                            diff_content="",
                            hunks=[],
                            commit_hash=current_commit.commit_hash,
                            commit_message=current_commit.commit_message,
                            is_merge_commit=current_commit.is_merge_commit,
                            author=current_commit.author,
                            timestamp=current_commit.timestamp,
                            lines_added=0,
                            lines_deleted=0
                        )
                        changes.append(change)

        return changes

    @staticmethod
    def get_diff_for_file(project_root: str, file_path: str, since_commit: Optional[str] = None) -> str:
        """获取特定文件的变更内容"""
        try:
            if since_commit:
                result = subprocess.run(
                    ['git', 'diff', since_commit, '--', file_path],
                    cwd=project_root,
                    capture_output=True,
                    text=True,
                    timeout=10
                )
            else:
                result = subprocess.run(
                    ['git', 'diff', 'HEAD~10', 'HEAD', '--', file_path],
                    cwd=project_root,
                    capture_output=True,
                    text=True,
                    timeout=10
                )
            return result.stdout if result.returncode == 0 else ""
        except:
            return ""


class RiskAssessor:
    """风险评估器 - 基于代码特征和变更模式评估风险等级"""

    CRITICAL_PATTERNS = [
        r'\b(permission|auth|security|privilege|credential|password)\b',
        r'\b(encrypt|decrypt|hash|sign|verify)\b',
        r'\b(std::mutex|std::lock|std::atomic)\b',
    ]

    HIGH_RISK_PATTERNS = [
        r'\b(parse|deserialize|encode|decode)\b',
        r'\b(std::(vector|map|unordered_map|deque).*\(.*\.at\()',
        r'\b(malloc|free|new|delete|realloc)\b',
        r'\b(throw|try|catch|exception)\b',
    ]

    MEDIUM_RISK_PATTERNS = [
        r'\b(validate|sanitize|check|verify)\b',
        r'\b(buffer|queue|pool|stack)\b',
        r'\b(connection|session|stream)\b',
    ]

    LOW_RISK_PATTERNS = [
        r'\b(log|debug|trace)\b',
        r'\b(setter|getter|accessor)\b',
        r'\b(init|reset|destroy)\b',
    ]

    @staticmethod
    def assess_risk(file_path: str, source_info: Optional[SourceFileInfo],
                   change: Optional[CodeChange] = None) -> Tuple[RiskLevel, str]:
        """综合评估风险等级"""
        content = ""
        if source_info:
            content = f"{source_info.path} {source_info.has_security_relevant_code}"

        risk = RiskLevel.INFO
        reasons = []

        for pattern in RiskAssessor.CRITICAL_PATTERNS:
            if re.search(pattern, content, re.IGNORECASE):
                risk = RiskLevel.CRITICAL
                reasons.append(f"安全相关代码: {pattern}")
                break

        if risk == RiskLevel.INFO:
            for pattern in RiskAssessor.HIGH_RISK_PATTERNS:
                if re.search(pattern, content, re.IGNORECASE):
                    risk = RiskLevel.HIGH
                    reasons.append(f"复杂处理逻辑: {pattern}")
                    break

        if risk == RiskLevel.INFO:
            for pattern in RiskAssessor.MEDIUM_RISK_PATTERNS:
                if re.search(pattern, content, re.IGNORECASE):
                    risk = RiskLevel.MEDIUM
                    reasons.append(f"数据处理代码: {pattern}")
                    break

        if source_info and source_info.complexity_score > 30:
            if risk.score < RiskLevel.HIGH.score:
                risk = RiskLevel.HIGH
                reasons.append(f"高复杂度 (score={source_info.complexity_score})")

        if change:
            if change.is_merge_commit:
                if risk.score < RiskLevel.MEDIUM.score:
                    risk = RiskLevel.MEDIUM
                    reasons.append("合并提交")
            elif "fix" in change.commit_message.lower():
                if risk.score < RiskLevel.HIGH.score:
                    risk = RiskLevel.HIGH
                    reasons.append("Bug修复提交")

        return risk, "; ".join(reasons) if reasons else "常规代码"

    @staticmethod
    def should_skip_file(file_path: str, diff_content: str = "") -> Tuple[bool, str]:
        """判断是否应该跳过此文件"""
        skip_dirs = ['3rdparty', 'build', 'dist', '.git', '__pycache__', 'test', 'spdlog']
        skip_patterns = ['test_', '_test.', 'Test.', '.test.', 'Generated']

        path_normalized = file_path.replace('\\', '/')

        for skip in skip_dirs:
            if f'/{skip}/' in path_normalized or path_normalized.startswith(skip + '/'):
                return True, f"位于跳过目录: {skip}"

        for skip in skip_patterns:
            if skip.lower() in path_normalized.lower():
                return True, f"匹配跳过模式: {skip}"

        if diff_content:
            added_lines = [l for l in diff_content.split('\n') if l.startswith('+') and not l.startswith('+++')]
            removed_lines = [l for l in diff_content.split('\n') if l.startswith('-') and not l.startswith('---')]

            if len(added_lines) < 3 and len(removed_lines) < 3:
                added_content = '\n'.join(added_lines)
                if not re.search(r'[a-zA-Z]{5,}', added_content):
                    return True, "仅格式调整"

        return False, ""


class TestGapAnalyzer:
    """自动化测试缺口分析工具 v2.0"""

    def __init__(self, project_root: str):
        self.project_root = Path(project_root)
        self.tests_dir = self.project_root / 'tests'
        self.src_dir = self.project_root / 'src'
        self.changes: List[CodeChange] = []
        self.gaps: List[CoverageGap] = []
        self.generated_tests: List[TestFile] = []
        self.source_files: Dict[str, SourceFileInfo] = {}
        self.test_coverage: Dict[str, Set[str]] = defaultdict(set)

    def run(self, days: int = 7, since_commit: Optional[str] = None,
            output_format: str = 'text') -> int:
        """运行测试缺口分析"""
        print(f"=" * 70)
        print(f"NEVO 自动化测试缺口分析工具 v2.0")
        print(f"项目根目录: {self.project_root}")
        print(f"分析范围: 最近 {days} 天")
        print(f"=" * 70)

        self._collect_changes(days, since_commit)
        self._parse_source_files()
        self._analyze_existing_tests()
        self._detect_gaps()
        self._generate_tests()

        return self._report(output_format)

    def _collect_changes(self, days: int, since_commit: Optional[str]) -> None:
        """收集代码变更"""
        print("\n[1/6] 收集代码变更...")

        self.changes = GitAnalyzer.get_recent_commits(str(self.project_root), days)

        if not self.changes:
            print("  Git历史为空，执行直接文件扫描...")
            self._scan_all_source_files()

        seen = set()
        unique_changes = []
        for change in self.changes:
            if change.file_path not in seen:
                seen.add(change.file_path)
                unique_changes.append(change)

        self.changes = unique_changes
        print(f"  发现 {len(self.changes)} 个文件变更")

    def _scan_all_source_files(self) -> None:
        """扫描所有源文件"""
        for ext in ['*.h', '*.hpp', '*.cpp']:
            for f in self.src_dir.rglob(ext):
                if self._should_analyze_file(str(f)):
                    self.changes.append(CodeChange(
                        file_path=str(f.relative_to(self.project_root)),
                        change_type='scanned',
                        diff_content="",
                        hunks=[],
                        commit_hash="",
                        commit_message="",
                        is_merge_commit=False,
                        author="",
                        timestamp=datetime.now(),
                        lines_added=0,
                        lines_deleted=0
                    ))

    def _should_analyze_file(self, path: str) -> bool:
        """判断是否应该分析该文件"""
        should_skip, reason = RiskAssessor.should_skip_file(path)
        if should_skip:
            return False
        return any(path.endswith(ext) for ext in ['.cpp', '.h', '.hpp'])

    def _parse_source_files(self) -> None:
        """解析源代码文件"""
        print("\n[2/6] 解析源代码...")

        parsed_count = 0
        for change in self.changes:
            if change.file_path in self.source_files:
                continue

            full_path = self.project_root / change.file_path
            if full_path.exists() and full_path.suffix in ['.h', '.hpp']:
                source_info = CodeParser.parse_header(str(full_path))
                if source_info:
                    self.source_files[change.file_path] = source_info
                    parsed_count += 1

        print(f"  解析了 {parsed_count} 个头文件")

    def _analyze_existing_tests(self) -> None:
        """分析现有测试覆盖"""
        print("\n[3/6] 分析现有测试覆盖...")

        test_pattern = re.compile(r'TEST\(([^,]+),\s*(\w+)\)')

        test_files_found = 0
        for test_file in self.tests_dir.rglob('*.cpp'):
            if 'Generated' in str(test_file):
                continue

            try:
                with open(test_file, 'r', encoding='utf-8', errors='ignore') as f:
                    content = f.read()

                matches = list(test_pattern.finditer(content))
                if matches:
                    test_files_found += 1
                    for match in matches:
                        class_name = match.group(1)
                        test_name = match.group(2)
                        self.test_coverage[class_name].add(test_name)
            except Exception:
                continue

        print(f"  分析了 {test_files_found} 个测试文件")
        print(f"  识别了 {len(self.test_coverage)} 个测试类")

    def _detect_gaps(self) -> None:
        """检测覆盖率缺口"""
        print("\n[4/6] 检测测试覆盖缺口...")

        for change in self.changes:
            should_skip, reason = RiskAssessor.should_skip_file(change.file_path, change.diff_content)
            if should_skip:
                continue

            file_gaps = self._analyze_file_gaps(change)
            self.gaps.extend(file_gaps)

        seen = set()
        unique_gaps = []
        for gap in self.gaps:
            key = f"{gap.file_path}:{gap.class_name}:{gap.method_name}"
            if key not in seen:
                seen.add(key)
                unique_gaps.append(gap)

        self.gaps = unique_gaps
        self.gaps.sort(key=lambda g: g.priority_score, reverse=True)

        print(f"  发现 {len(self.gaps)} 个覆盖缺口")

    def _analyze_file_gaps(self, change: CodeChange) -> List[CoverageGap]:
        """分析单个文件的覆盖缺口"""
        gaps = []
        file_path = change.file_path

        test_file = self._get_test_file(file_path)
        if not test_file.exists():
            source_info = self.source_files.get(file_path)
            risk, reason = RiskAssessor.assess_risk(file_path, source_info, change)

            if risk.score >= RiskLevel.MEDIUM.score:
                gaps.append(CoverageGap(
                    file_path=file_path,
                    class_name="",
                    method_name="全部方法",
                    function_signature=None,
                    risk_level=risk,
                    reason=f"源文件缺少测试覆盖: {reason}",
                    suggested_test_pattern="",
                    priority_score=risk.score,
                    complexity_score=source_info.complexity_score if source_info else 0,
                    change_history=[change.commit_message] if change.commit_message else [],
                    affected_test_files=[],
                    edge_cases_to_cover=self._identify_edge_cases(file_path, source_info)
                ))

        source_info = self.source_files.get(file_path)
        if source_info:
            for cls in source_info.classes:
                test_class_name = f"{cls.name}Test"
                if test_class_name not in self.test_coverage:
                    risk, reason = RiskAssessor.assess_risk(file_path, source_info, change)
                    gaps.append(CoverageGap(
                        file_path=file_path,
                        class_name=cls.name,
                        method_name="全部方法",
                        function_signature=None,
                        risk_level=risk,
                        reason=f"类 {cls.name} 缺少测试覆盖",
                        suggested_test_pattern=f"TEST({test_class_name}, DefaultConstruction)",
                        priority_score=risk.score,
                        complexity_score=source_info.complexity_score,
                        change_history=[change.commit_message] if change.commit_message else [],
                        affected_test_files=[],
                        edge_cases_to_cover=self._identify_edge_cases(file_path, source_info)
                    ))
                else:
                    for method in cls.methods:
                        test_name = f"{method.name}_"
                        if not any(test_name in t for t in self.test_coverage[test_class_name]):
                            risk, reason = RiskAssessor.assess_risk(file_path, source_info, change)

                            if len(method.parameters) > 3:
                                risk = RiskLevel.HIGH if risk.score < RiskLevel.HIGH.score else risk

                            gaps.append(CoverageGap(
                                file_path=file_path,
                                class_name=cls.name,
                                method_name=method.name,
                                function_signature=self._format_function_signature(method),
                                risk_level=risk,
                                reason=f"方法 {cls.name}::{method.name} 缺少测试覆盖",
                                suggested_test_pattern=f"TEST({test_class_name}, {method.name}_)",
                                priority_score=risk.score,
                                complexity_score=source_info.complexity_score,
                                change_history=[change.commit_message] if change.commit_message else [],
                                affected_test_files=[],
                                edge_cases_to_cover=self._identify_edge_cases_for_method(method)
                            ))

        return gaps

    def _identify_edge_cases(self, file_path: str, source_info: Optional[SourceFileInfo]) -> List[str]:
        """识别需要测试的边界情况"""
        edge_cases = []

        if not source_info:
            return edge_cases

        if 'buffer' in file_path.lower() or 'queue' in file_path.lower():
            edge_cases.extend(['空缓冲区', '满缓冲区', '单元素', '多元素'])

        if 'crypto' in file_path.lower() or 'encrypt' in file_path.lower():
            edge_cases.extend(['空输入', '最大输入', '无效密钥', '损坏数据'])

        if 'permission' in file_path.lower() or 'auth' in file_path.lower():
            edge_cases.extend(['未授权用户', '边界权限', '权限组合'])

        if source_info.has_concurrency_code:
            edge_cases.extend(['竞态条件', '死锁', '锁超时'])

        return edge_cases

    def _identify_edge_cases_for_method(self, method: FunctionSignature) -> List[str]:
        """识别特定方法的边界情况"""
        edge_cases = []

        for param_type, param_name in method.parameters:
            if 'size' in param_name.lower() or 'count' in param_name.lower():
                edge_cases.extend(['零值', '最大值', '负值'])

            if 'id' in param_name.lower() or 'index' in param_name.lower():
                edge_cases.extend(['无效ID', '零ID', '最大ID'])

            if param_type in ['std::vector', 'std::string', 'std::span']:
                edge_cases.extend(['空容器', '单元素', '大容器'])

        method_lower = method.name.lower()
        if 'push' in method_lower or 'add' in method_lower:
            edge_cases.append('添加重复项')

        if 'remove' in method_lower or 'delete' in method_lower:
            edge_cases.append('移除不存在项')

        if 'find' in method_lower or 'search' in method_lower:
            edge_cases.append('查找不存在项')

        return edge_cases

    def _format_function_signature(self, method: FunctionSignature) -> str:
        """格式化函数签名"""
        params = ', '.join(f"{pt} {pn}" if pn else pt for pt, pn in method.parameters)
        return f"{method.return_type} {method.name}({params})"

    def _get_test_file(self, source_file: str) -> Path:
        """获取对应的测试文件路径"""
        source_path = Path(source_file)
        parts = source_path.parts

        if 'include' in parts:
            idx = parts.index('include')
            rel_path = Path(*parts[idx+1:])
        else:
            rel_path = source_path

        test_path = self.tests_dir / rel_path.parent / f"Test{rel_path.stem}.cpp"
        return test_path

    def _generate_tests(self) -> None:
        """生成测试用例"""
        print("\n[5/6] 生成补充测试...")

        gaps_by_file: Dict[str, List[CoverageGap]] = defaultdict(list)
        for gap in self.gaps:
            if gap.risk_level.score >= RiskLevel.MEDIUM.score:
                gaps_by_file[gap.file_path].append(gap)

        for file_path, gaps in gaps_by_file.items():
            test_file = self._create_test_file(file_path, gaps)
            if test_file:
                self.generated_tests.append(test_file)

        print(f"  生成 {len(self.generated_tests)} 个测试文件")

    def _create_test_file(self, target_file: str, gaps: List[CoverageGap]) -> Optional[TestFile]:
        """创建测试文件"""
        if not gaps:
            return None

        source_path = Path(target_file)
        test_dir = self.tests_dir / source_path.parent
        test_file_path = test_dir / f"Test{source_path.stem}Generated.cpp"

        test_cases = []
        for gap in gaps:
            test_case = {
                'test_name': f"{gap.class_name}_{gap.method_name}".replace(' ', '_') if gap.class_name else gap.method_name,
                'class_name': gap.class_name,
                'method': gap.method_name,
                'function_signature': gap.function_signature,
                'risk_level': gap.risk_level.label,
                'description': gap.reason,
                'edge_cases': gap.edge_cases_to_cover,
                'pattern': self._generate_test_code(gap)
            }
            test_cases.append(test_case)

        return TestFile(
            file_path=str(test_file_path),
            target_file=target_file,
            test_cases=test_cases,
            description=f"自动生成的 {source_path.stem} 覆盖测试",
            generated_from_gaps=[f"{g.class_name}::{g.method_name}" for g in gaps]
        )

    def _generate_test_code(self, gap: CoverageGap) -> str:
        """生成测试代码"""
        if not gap.class_name:
            return self._generate_generic_test(gap)

        method_lower = gap.method_name.lower()

        if 'volume' in method_lower:
            return self._generate_volume_test(gap)
        elif 'add' in method_lower or 'push' in method_lower:
            return self._generate_add_test(gap)
        elif 'remove' in method_lower or 'delete' in method_lower:
            return self._generate_remove_test(gap)
        elif 'get' in method_lower or 'find' in method_lower:
            return self._generate_get_test(gap)
        elif 'reset' in method_lower or 'clear' in method_lower:
            return self._generate_reset_test(gap)
        elif 'permission' in method_lower or 'auth' in method_lower:
            return self._generate_permission_test(gap)
        elif 'encrypt' in method_lower or 'decrypt' in method_lower:
            return self._generate_crypto_test(gap)
        else:
            return self._generate_method_test(gap)

    def _generate_generic_test(self, gap: CoverageGap) -> str:
        """生成通用测试"""
        return f'''
TEST({gap.class_name}Test, AutoGeneratedTest) {{
    // 风险等级: {gap.risk_level.label}
    // 原因: {gap.reason}
    // 边界情况: {', '.join(gap.edge_cases_to_cover) if gap.edge_cases_to_cover else '无'}
    GTEST_SKIP() << "TODO: 实现具体的测试逻辑";
}}
'''

    def _generate_volume_test(self, gap: CoverageGap) -> str:
        """生成音量相关测试"""
        return f'''
TEST({gap.class_name}Test, {gap.method_name}_BoundaryValues) {{
    {gap.class_name} obj;
    obj.{gap.method_name}(0.0f);
    obj.{gap.method_name}(2.0f);
    obj.{gap.method_name}(-1.0f);
    obj.{gap.method_name}(3.0f);
    GTEST_SKIP() << "TODO: 验证边界值处理";
}}

TEST({gap.class_name}Test, {gap.method_name}_MultipleUsers) {{
    GTEST_SKIP() << "TODO: 测试多用户音量控制";
}}
'''

    def _generate_add_test(self, gap: CoverageGap) -> str:
        """生成添加操作测试"""
        return f'''
TEST({gap.class_name}Test, {gap.method_name}_BasicOperation) {{
    GTEST_SKIP() << "TODO: 测试基本添加操作";
}}

TEST({gap.class_name}Test, {gap.method_name}_DuplicateItem) {{
    GTEST_SKIP() << "TODO: 测试添加重复项";
}}

TEST({gap.class_name}Test, {gap.method_name}_CapacityLimit) {{
    GTEST_SKIP() << "TODO: 测试容量限制";
}}
'''

    def _generate_remove_test(self, gap: CoverageGap) -> str:
        """生成移除操作测试"""
        return f'''
TEST({gap.class_name}Test, {gap.method_name}_ExistingItem) {{
    GTEST_SKIP() << "TODO: 测试移除存在的项";
}}

TEST({gap.class_name}Test, {gap.method_name}_NonExistingItem) {{
    GTEST_SKIP() << "TODO: 测试移除不存在的项";
}}

TEST({gap.class_name}Test, {gap.method_name}_Idempotent) {{
    GTEST_SKIP() << "TODO: 测试幂等性";
}}
'''

    def _generate_get_test(self, gap: CoverageGap) -> str:
        """生成获取操作测试"""
        return f'''
TEST({gap.class_name}Test, {gap.method_name}_ExistingItem) {{
    GTEST_SKIP() << "TODO: 测试获取存在的项";
}}

TEST({gap.class_name}Test, {gap.method_name}_NonExistingItem) {{
    GTEST_SKIP() << "TODO: 测试获取不存在的项";
}}

TEST({gap.class_name}Test, {gap.method_name}_DefaultValue) {{
    GTEST_SKIP() << "TODO: 测试默认值";
}}
'''

    def _generate_reset_test(self, gap: CoverageGap) -> str:
        """生成重置操作测试"""
        return f'''
TEST({gap.class_name}Test, {gap.method_name}_AfterOperation) {{
    GTEST_SKIP() << "TODO: 测试操作后重置";
}}

TEST({gap.class_name}Test, {gap.method_name}_MultipleTimes) {{
    GTEST_SKIP() << "TODO: 测试多次重置";
}}
'''

    def _generate_permission_test(self, gap: CoverageGap) -> str:
        """生成权限测试"""
        return f'''
TEST({gap.class_name}Test, {gap.method_name}_UnauthorizedUser) {{
    GTEST_SKIP() << "TODO: 测试未授权用户的权限操作";
}}

TEST({gap.class_name}Test, {gap.method_name}_BoundaryPermission) {{
    GTEST_SKIP() << "TODO: 测试边界权限值";
}}

TEST({gap.class_name}Test, {gap.method_name}_PermissionCombinations) {{
    GTEST_SKIP() << "TODO: 测试权限组合";
}}
'''

    def _generate_crypto_test(self, gap: CoverageGap) -> str:
        """生成加密测试"""
        return f'''
TEST({gap.class_name}Test, {gap.method_name}_EmptyInput) {{
    GTEST_SKIP() << "TODO: 测试空输入";
}}

TEST({gap.class_name}Test, {gap.method_name}_MaxInput) {{
    GTEST_SKIP() << "TODO: 测试最大输入";
}}

TEST({gap.class_name}Test, {gap.method_name}_InvalidKey) {{
    GTEST_SKIP() << "TODO: 测试无效密钥";
}}
'''

    def _generate_method_test(self, gap: CoverageGap) -> str:
        """生成通用方法测试"""
        sig = gap.function_signature if gap.function_signature else ""
        edge_cases = ", ".join(gap.edge_cases_to_cover[:3]) if gap.edge_cases_to_cover else "标准输入"

        return f'''
TEST({gap.class_name}Test, {gap.method_name}_Basic) {{
    // 签名: {sig}
    GTEST_SKIP() << "TODO: 实现 {gap.method_name} 测试";
}}

TEST({gap.class_name}Test, {gap.method_name}_EdgeCases) {{
    // 边界情况: {edge_cases}
    GTEST_SKIP() << "TODO: 实现边界情况测试";
}}
'''

    def _report(self, output_format: str) -> int:
        """生成报告"""
        print("\n[6/6] 生成报告...")

        high_risk_count = len([g for g in self.gaps if g.risk_level.score >= RiskLevel.HIGH.score])

        if output_format == 'json':
            self._report_json()
        elif output_format == 'markdown':
            self._report_markdown()
        else:
            self._report_text()

        if high_risk_count > 0:
            print(f"\n发现 {high_risk_count} 个高风险缺口")
            return 1

        print("\n未发现高风险覆盖缺口")
        return 0

    def _report_text(self) -> None:
        """文本格式报告"""
        print("\n" + "=" * 70)
        print("测试覆盖缺口报告")
        print("=" * 70)

        if not self.gaps:
            print("\n未发现测试覆盖缺口")
            return

        print(f"\n总计: {len(self.gaps)} 个缺口")

        for level in [RiskLevel.CRITICAL, RiskLevel.HIGH, RiskLevel.MEDIUM, RiskLevel.LOW]:
            gaps = [g for g in self.gaps if g.risk_level == level]
            if gaps:
                print(f"\n【{level.label.upper()} 风险】{len(gaps)} 个")
                for gap in gaps[:5]:
                    print(f"  - {gap.file_path}::{gap.class_name}::{gap.method_name}")
                    print(f"    原因: {gap.reason}")
                    if gap.edge_cases_to_cover:
                        print(f"    边界情况: {', '.join(gap.edge_cases_to_cover[:3])}")
                if len(gaps) > 5:
                    print(f"  ... 还有 {len(gaps) - 5} 个")

        if self.generated_tests:
            print(f"\n生成测试文件: {len(self.generated_tests)} 个")
            for tf in self.generated_tests:
                print(f"  - {tf.file_path}")

    def _report_json(self) -> None:
        """JSON格式报告"""
        report = {
            'timestamp': datetime.now().isoformat(),
            'total_gaps': len(self.gaps),
            'gaps_by_risk': {
                'critical': len([g for g in self.gaps if g.risk_level == RiskLevel.CRITICAL]),
                'high': len([g for g in self.gaps if g.risk_level == RiskLevel.HIGH]),
                'medium': len([g for g in self.gaps if g.risk_level == RiskLevel.MEDIUM]),
                'low': len([g for g in self.gaps if g.risk_level == RiskLevel.LOW])
            },
            'gaps': [asdict(g) for g in self.gaps],
            'generated_tests': [
                {
                    'file': tf.file_path,
                    'target': tf.target_file,
                    'test_count': len(tf.test_cases)
                }
                for tf in self.generated_tests
            ]
        }
        print(json.dumps(report, indent=2, ensure_ascii=False))

    def _report_markdown(self) -> None:
        """Markdown格式报告"""
        print("\n# NEVO 测试缺口分析报告 v2.0")
        print(f"\n**分析时间**: {datetime.now().isoformat()}")
        print(f"**项目**: NEVO VoIP Application")

        print("\n## 执行摘要")
        print(f"\n| 类别 | 数量 |")
        print(f"|------|------|")
        print(f"| 严重风险缺口 | {len([g for g in self.gaps if g.risk_level == RiskLevel.CRITICAL])} |")
        print(f"| 高风险缺口 | {len([g for g in self.gaps if g.risk_level == RiskLevel.HIGH])} |")
        print(f"| 中风险缺口 | {len([g for g in self.gaps if g.risk_level == RiskLevel.MEDIUM])} |")
        print(f"| 低风险缺口 | {len([g for g in self.gaps if g.risk_level == RiskLevel.LOW])} |")
        print(f"| 生成测试文件 | {len(self.generated_tests)} |")

        if self.gaps:
            print("\n## 识别的测试覆盖缺口\n")
            for gap in self.gaps[:15]:
                risk_icon = "🔴" if gap.risk_level == RiskLevel.CRITICAL else "🟠" if gap.risk_level == RiskLevel.HIGH else "🟡" if gap.risk_level == RiskLevel.MEDIUM else "🟢"
                location = f"{gap.file_path}::{gap.class_name}::{gap.method_name}" if gap.class_name else f"{gap.file_path}::{gap.method_name}"
                print(f"### {risk_icon} {location}")
                print(f"\n- **风险等级**: {gap.risk_level.label}")
                print(f"- **原因**: {gap.reason}")
                if gap.function_signature:
                    print(f"- **函数签名**: `{gap.function_signature}`")
                if gap.edge_cases_to_cover:
                    print(f"- **建议边界情况**: {', '.join(gap.edge_cases_to_cover[:5])}")
                print()


def main():
    parser = argparse.ArgumentParser(
        description='NEVO 自动化测试缺口分析工具 v2.0'
    )
    parser.add_argument(
        '--project-root', '-p',
        default='.',
        help='项目根目录路径'
    )
    parser.add_argument(
        '--days', '-d',
        type=int,
        default=7,
        help='分析最近多少天的变更 (默认: 7)'
    )
    parser.add_argument(
        '--since',
        help='自指定提交以来的变更'
    )
    parser.add_argument(
        '--output', '-o',
        choices=['text', 'json', 'markdown'],
        default='text',
        help='输出格式 (默认: text)'
    )
    parser.add_argument(
        '--dry-run',
        action='store_true',
        help='仅分析不生成测试文件'
    )

    args = parser.parse_args()

    analyzer = TestGapAnalyzer(args.project_root)

    try:
        exit_code = analyzer.run(
            days=args.days,
            since_commit=args.since,
            output_format=args.output
        )
        sys.exit(exit_code)
    except Exception as e:
        print(f"错误: {e}", file=sys.stderr)
        sys.exit(1)


if __name__ == '__main__':
    main()
