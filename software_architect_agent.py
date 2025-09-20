#!/usr/bin/env python3
"""
Software Architect Agent - Processes documents and provides architectural guidance.
"""

import json
import time
from datetime import datetime
from pathlib import Path

class SoftwareArchitectAgent:
    def __init__(self):
        self.responses_dir = "/workspace/workflow_claude/responses"
        os.makedirs(self.responses_dir, exist_ok=True)
    
    def process_document(self, document_content, original_file_path):
        """Process a document and provide architectural guidance"""
        
        # Analyze the document content
        analysis = self.analyze_document(document_content)
        
        # Generate architectural guidance
        guidance = self.generate_guidance(analysis, original_file_path)
        
        # Create response
        response = {
            "timestamp": datetime.now().isoformat(),
            "original_file": original_file_path,
            "analysis": analysis,
            "architectural_guidance": guidance,
            "recommendations": self.generate_recommendations(analysis),
            "status": "completed"
        }
        
        return response
    
    def analyze_document(self, content):
        """Analyze the document content for architectural patterns and concerns"""
        analysis = {
            "content_type": self.detect_content_type(content),
            "key_components": self.extract_components(content),
            "architectural_patterns": self.identify_patterns(content),
            "potential_issues": self.identify_issues(content),
            "complexity_score": self.assess_complexity(content)
        }
        return analysis
    
    def detect_content_type(self, content):
        """Detect the type of document content"""
        content_lower = content.lower()
        if "api" in content_lower or "endpoint" in content_lower:
            return "API Documentation"
        elif "database" in content_lower or "sql" in content_lower:
            return "Database Schema"
        elif "class" in content_lower or "function" in content_lower:
            return "Code Documentation"
        elif "workflow" in content_lower or "process" in content_lower:
            return "Process Documentation"
        else:
            return "General Documentation"
    
    def extract_components(self, content):
        """Extract key architectural components from the content"""
        components = []
        lines = content.split('\n')
        
        for line in lines:
            line = line.strip()
            if any(keyword in line.lower() for keyword in ['service', 'module', 'component', 'system', 'interface']):
                components.append(line)
        
        return components[:10]  # Limit to top 10 components
    
    def identify_patterns(self, content):
        """Identify architectural patterns in the content"""
        patterns = []
        content_lower = content.lower()
        
        if "microservice" in content_lower:
            patterns.append("Microservices Architecture")
        if "mvc" in content_lower or "model-view-controller" in content_lower:
            patterns.append("MVC Pattern")
        if "repository" in content_lower:
            patterns.append("Repository Pattern")
        if "factory" in content_lower:
            patterns.append("Factory Pattern")
        if "observer" in content_lower:
            patterns.append("Observer Pattern")
        
        return patterns if patterns else ["No specific patterns detected"]
    
    def identify_issues(self, content):
        """Identify potential architectural issues"""
        issues = []
        content_lower = content.lower()
        
        if "hardcoded" in content_lower:
            issues.append("Hardcoded values detected")
        if "tightly coupled" in content_lower:
            issues.append("Tight coupling concerns")
        if "single responsibility" in content_lower:
            issues.append("Single Responsibility Principle considerations")
        if "scalability" in content_lower:
            issues.append("Scalability concerns mentioned")
        
        return issues if issues else ["No obvious issues detected"]
    
    def assess_complexity(self, content):
        """Assess the complexity of the content"""
        lines = len(content.split('\n'))
        words = len(content.split())
        
        if lines < 50 and words < 500:
            return "Low"
        elif lines < 200 and words < 2000:
            return "Medium"
        else:
            return "High"
    
    def generate_guidance(self, analysis, original_file_path):
        """Generate architectural guidance based on analysis"""
        guidance = []
        
        # Content type specific guidance
        if analysis["content_type"] == "API Documentation":
            guidance.append("Consider implementing proper API versioning and documentation standards")
            guidance.append("Ensure proper error handling and response formats")
        elif analysis["content_type"] == "Database Schema":
            guidance.append("Review normalization and indexing strategies")
            guidance.append("Consider data migration and backup strategies")
        elif analysis["content_type"] == "Code Documentation":
            guidance.append("Ensure proper separation of concerns")
            guidance.append("Consider implementing design patterns for maintainability")
        
        # Pattern-specific guidance
        for pattern in analysis["architectural_patterns"]:
            if pattern == "Microservices Architecture":
                guidance.append("Ensure proper service boundaries and communication protocols")
            elif pattern == "MVC Pattern":
                guidance.append("Maintain clear separation between model, view, and controller layers")
        
        # Complexity-based guidance
        if analysis["complexity_score"] == "High":
            guidance.append("Consider breaking down into smaller, more manageable components")
            guidance.append("Implement comprehensive testing and monitoring")
        
        return guidance if guidance else ["Continue with current architectural approach"]
    
    def generate_recommendations(self, analysis):
        """Generate specific recommendations"""
        recommendations = []
        
        if analysis["complexity_score"] == "High":
            recommendations.append("Consider refactoring to reduce complexity")
        
        if not analysis["architectural_patterns"] or analysis["architectural_patterns"] == ["No specific patterns detected"]:
            recommendations.append("Consider implementing established architectural patterns")
        
        if analysis["potential_issues"] and analysis["potential_issues"] != ["No obvious issues detected"]:
            recommendations.append("Address identified architectural concerns")
        
        recommendations.append("Implement comprehensive documentation and testing")
        recommendations.append("Consider code review and architectural review processes")
        
        return recommendations
    
    def save_response(self, response, original_file_path):
        """Save the response to a file"""
        filename = f"architect_response_{Path(original_file_path).stem}_{int(time.time())}.json"
        filepath = os.path.join(self.responses_dir, filename)
        
        with open(filepath, 'w', encoding='utf-8') as f:
            json.dump(response, f, indent=2)
        
        return filepath

# Import os for the save_response method
import os