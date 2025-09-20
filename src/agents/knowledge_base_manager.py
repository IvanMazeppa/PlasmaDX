#!/usr/bin/env python3
"""
Knowledge Base Manager for DX12 Technique Agent

Manages the persistent knowledge base, handles updates, and provides
query interfaces for discovered techniques.

Author: AI Assistant
Created: 2025-01-19
"""

import json
import sqlite3
import logging
from datetime import datetime, timedelta
from typing import Dict, List, Optional, Any, Tuple
from pathlib import Path
from dataclasses import asdict, dataclass
import hashlib

logger = logging.getLogger('KnowledgeBaseManager')

@dataclass
class TechniqueMetadata:
    """Metadata for a technique entry"""
    id: str
    name: str
    category: str
    relevance_score: float
    discovery_date: str
    last_updated: str
    usage_count: int
    tags: List[str]
    project_applications: List[str]

@dataclass
class SearchResult:
    """Search result with relevance score"""
    technique: Dict[str, Any]
    relevance_score: float
    match_reasons: List[str]

class KnowledgeBaseManager:
    """Manages the persistent knowledge base for discovered techniques"""
    
    def __init__(self, db_path: str = "findings/technique_knowledge_base.db"):
        self.db_path = Path(db_path)
        self.db_path.parent.mkdir(exist_ok=True)
        self.logger = logging.getLogger('KnowledgeBaseManager')
        
        # Initialize database
        self._init_database()
        
        # Cache for frequently accessed data
        self._cache = {}
        self._cache_ttl = timedelta(minutes=30)
        
    def _init_database(self):
        """Initialize the SQLite database schema"""
        try:
            with sqlite3.connect(self.db_path) as conn:
                cursor = conn.cursor()
                
                # Techniques table
                cursor.execute('''
                    CREATE TABLE IF NOT EXISTS techniques (
                        id TEXT PRIMARY KEY,
                        name TEXT NOT NULL,
                        category TEXT NOT NULL,
                        description TEXT,
                        relevance_score REAL NOT NULL,
                        project_applications TEXT,  -- JSON array
                        api_references TEXT,        -- JSON array
                        shader_stages TEXT,         -- JSON array
                        tags TEXT,                  -- JSON array
                        examples TEXT,              -- JSON array
                        discovery_date TEXT NOT NULL,
                        last_updated TEXT NOT NULL,
                        usage_count INTEGER DEFAULT 0,
                        last_accessed TEXT
                    )
                ''')
                
                # Discovery history table
                cursor.execute('''
                    CREATE TABLE IF NOT EXISTS discovery_history (
                        id INTEGER PRIMARY KEY AUTOINCREMENT,
                        timestamp TEXT NOT NULL,
                        operation TEXT NOT NULL,
                        technique_id TEXT,
                        details TEXT
                    )
                ''')
                
                # Search cache table
                cursor.execute('''
                    CREATE TABLE IF NOT EXISTS search_cache (
                        query_hash TEXT PRIMARY KEY,
                        query_text TEXT NOT NULL,
                        results TEXT,  -- JSON
                        timestamp TEXT NOT NULL,
                        expires_at TEXT NOT NULL
                    )
                ''')
                
                # Project context table
                cursor.execute('''
                    CREATE TABLE IF NOT EXISTS project_context (
                        key TEXT PRIMARY KEY,
                        value TEXT NOT NULL,
                        updated_at TEXT NOT NULL
                    )
                ''')
                
                conn.commit()
                self.logger.info("Database initialized successfully")
                
        except Exception as e:
            self.logger.error(f"Failed to initialize database: {e}")
            raise
    
    def _generate_technique_id(self, name: str, category: str) -> str:
        """Generate a unique ID for a technique"""
        return hashlib.md5(f"{category}:{name}".encode()).hexdigest()
    
    def store_technique(self, technique: Dict[str, Any]) -> str:
        """Store a technique in the knowledge base"""
        try:
            technique_id = self._generate_technique_id(technique['name'], technique['category'])
            
            with sqlite3.connect(self.db_path) as conn:
                cursor = conn.cursor()
                
                # Check if technique already exists
                cursor.execute('SELECT id FROM techniques WHERE id = ?', (technique_id,))
                exists = cursor.fetchone() is not None
                
                if exists:
                    # Update existing technique
                    cursor.execute('''
                        UPDATE techniques SET
                            description = ?,
                            relevance_score = ?,
                            project_applications = ?,
                            api_references = ?,
                            shader_stages = ?,
                            tags = ?,
                            examples = ?,
                            last_updated = ?
                        WHERE id = ?
                    ''', (
                        technique.get('description', ''),
                        technique.get('relevance_score', 0.0),
                        json.dumps(technique.get('project_applications', [])),
                        json.dumps(technique.get('api_references', [])),
                        json.dumps(technique.get('shader_stages', [])),
                        json.dumps(technique.get('tags', [])),
                        json.dumps(technique.get('examples', [])),
                        datetime.now().isoformat(),
                        technique_id
                    ))
                    
                    self.logger.info(f"Updated technique: {technique['name']}")
                else:
                    # Insert new technique
                    cursor.execute('''
                        INSERT INTO techniques (
                            id, name, category, description, relevance_score,
                            project_applications, api_references, shader_stages,
                            tags, examples, discovery_date, last_updated
                        ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
                    ''', (
                        technique_id,
                        technique['name'],
                        technique['category'],
                        technique.get('description', ''),
                        technique.get('relevance_score', 0.0),
                        json.dumps(technique.get('project_applications', [])),
                        json.dumps(technique.get('api_references', [])),
                        json.dumps(technique.get('shader_stages', [])),
                        json.dumps(technique.get('tags', [])),
                        json.dumps(technique.get('examples', [])),
                        datetime.now().isoformat(),
                        datetime.now().isoformat()
                    ))
                    
                    self.logger.info(f"Stored new technique: {technique['name']}")
                
                # Log discovery history
                cursor.execute('''
                    INSERT INTO discovery_history (timestamp, operation, technique_id, details)
                    VALUES (?, ?, ?, ?)
                ''', (
                    datetime.now().isoformat(),
                    'store' if not exists else 'update',
                    technique_id,
                    json.dumps({'name': technique['name'], 'category': technique['category']})
                ))
                
                conn.commit()
                
            # Clear cache
            self._clear_cache()
            
            return technique_id
            
        except Exception as e:
            self.logger.error(f"Failed to store technique: {e}")
            raise
    
    def get_technique(self, technique_id: str) -> Optional[Dict[str, Any]]:
        """Get a technique by ID"""
        try:
            with sqlite3.connect(self.db_path) as conn:
                cursor = conn.cursor()
                cursor.execute('SELECT * FROM techniques WHERE id = ?', (technique_id,))
                row = cursor.fetchone()
                
                if row:
                    # Update usage count and last accessed
                    cursor.execute('''
                        UPDATE techniques SET 
                            usage_count = usage_count + 1,
                            last_accessed = ?
                        WHERE id = ?
                    ''', (datetime.now().isoformat(), technique_id))
                    conn.commit()
                    
                    return self._row_to_technique(row)
                    
        except Exception as e:
            self.logger.error(f"Failed to get technique: {e}")
            
        return None
    
    def search_techniques(self, query: str, limit: int = 10, 
                         category_filter: Optional[str] = None,
                         min_relevance: float = 0.0) -> List[SearchResult]:
        """Search techniques with relevance scoring"""
        try:
            # Check cache first
            cache_key = f"search:{hashlib.md5(query.encode()).hexdigest()}"
            cached_result = self._get_from_cache(cache_key)
            if cached_result:
                return cached_result
            
            query_lower = query.lower()
            results = []
            
            with sqlite3.connect(self.db_path) as conn:
                cursor = conn.cursor()
                
                # Build query
                where_clauses = []
                params = []
                
                if category_filter:
                    where_clauses.append("category = ?")
                    params.append(category_filter)
                
                if min_relevance > 0:
                    where_clauses.append("relevance_score >= ?")
                    params.append(min_relevance)
                
                where_clause = " AND ".join(where_clauses) if where_clauses else "1=1"
                
                cursor.execute(f'''
                    SELECT * FROM techniques 
                    WHERE {where_clause}
                    ORDER BY relevance_score DESC
                ''', params)
                
                rows = cursor.fetchall()
                
                for row in rows:
                    technique = self._row_to_technique(row)
                    score, reasons = self._calculate_search_score(query_lower, technique)
                    
                    if score > 0:
                        results.append(SearchResult(
                            technique=technique,
                            relevance_score=score,
                            match_reasons=reasons
                        ))
                
                # Sort by relevance score
                results.sort(key=lambda x: x.relevance_score, reverse=True)
                results = results[:limit]
            
            # Cache results
            self._set_cache(cache_key, results)
            
            return results
            
        except Exception as e:
            self.logger.error(f"Search failed: {e}")
            return []
    
    def _calculate_search_score(self, query: str, technique: Dict[str, Any]) -> Tuple[float, List[str]]:
        """Calculate search relevance score and match reasons"""
        score = 0.0
        reasons = []
        
        # Name matching (highest weight)
        if query in technique['name'].lower():
            score += 3.0
            reasons.append("name_match")
        
        # Description matching
        if query in technique.get('description', '').lower():
            score += 2.0
            reasons.append("description_match")
        
        # Tag matching
        for tag in technique.get('tags', []):
            if query in tag.lower():
                score += 1.5
                reasons.append(f"tag_match:{tag}")
        
        # Application matching
        for app in technique.get('project_applications', []):
            if query in app.lower():
                score += 1.2
                reasons.append(f"application_match:{app[:30]}")
        
        # Category matching
        if query in technique['category'].lower():
            score += 1.0
            reasons.append("category_match")
        
        # Boost by base relevance score
        score += technique.get('relevance_score', 0.0) * 0.5
        
        return score, reasons
    
    def _row_to_technique(self, row: Tuple) -> Dict[str, Any]:
        """Convert database row to technique dictionary"""
        return {
            'id': row[0],
            'name': row[1],
            'category': row[2],
            'description': row[3],
            'relevance_score': row[4],
            'project_applications': json.loads(row[5] or '[]'),
            'api_references': json.loads(row[6] or '[]'),
            'shader_stages': json.loads(row[7] or '[]'),
            'tags': json.loads(row[8] or '[]'),
            'examples': json.loads(row[9] or '[]'),
            'discovery_date': row[10],
            'last_updated': row[11],
            'usage_count': row[12],
            'last_accessed': row[13]
        }
    
    def get_statistics(self) -> Dict[str, Any]:
        """Get knowledge base statistics"""
        try:
            with sqlite3.connect(self.db_path) as conn:
                cursor = conn.cursor()
                
                # Total techniques
                cursor.execute('SELECT COUNT(*) FROM techniques')
                total_techniques = cursor.fetchone()[0]
                
                # Techniques by category
                cursor.execute('''
                    SELECT category, COUNT(*) 
                    FROM techniques 
                    GROUP BY category 
                    ORDER BY COUNT(*) DESC
                ''')
                categories = dict(cursor.fetchall())
                
                # High relevance techniques
                cursor.execute('SELECT COUNT(*) FROM techniques WHERE relevance_score >= 0.8')
                high_relevance = cursor.fetchone()[0]
                
                # Recent discoveries
                cursor.execute('''
                    SELECT COUNT(*) FROM techniques 
                    WHERE discovery_date >= date('now', '-7 days')
                ''')
                recent_discoveries = cursor.fetchone()[0]
                
                # Most used techniques
                cursor.execute('''
                    SELECT name, usage_count 
                    FROM techniques 
                    ORDER BY usage_count DESC 
                    LIMIT 5
                ''')
                most_used = cursor.fetchall()
                
                return {
                    'total_techniques': total_techniques,
                    'categories': categories,
                    'high_relevance_count': high_relevance,
                    'recent_discoveries': recent_discoveries,
                    'most_used_techniques': most_used,
                    'last_updated': datetime.now().isoformat()
                }
                
        except Exception as e:
            self.logger.error(f"Failed to get statistics: {e}")
            return {}
    
    def export_knowledge_base(self, export_path: str):
        """Export knowledge base to JSON file"""
        try:
            with sqlite3.connect(self.db_path) as conn:
                cursor = conn.cursor()
                cursor.execute('SELECT * FROM techniques')
                rows = cursor.fetchall()
                
                techniques = []
                for row in rows:
                    techniques.append(self._row_to_technique(row))
                
                export_data = {
                    'metadata': {
                        'export_date': datetime.now().isoformat(),
                        'total_techniques': len(techniques),
                        'export_version': '1.0'
                    },
                    'techniques': techniques
                }
                
                with open(export_path, 'w') as f:
                    json.dump(export_data, f, indent=2)
                
                self.logger.info(f"Knowledge base exported to {export_path}")
                
        except Exception as e:
            self.logger.error(f"Export failed: {e}")
            raise
    
    def _clear_cache(self):
        """Clear the in-memory cache"""
        self._cache.clear()
    
    def _get_from_cache(self, key: str) -> Optional[Any]:
        """Get item from cache if not expired"""
        if key in self._cache:
            item, timestamp = self._cache[key]
            if datetime.now() - timestamp < self._cache_ttl:
                return item
            else:
                del self._cache[key]
        return None
    
    def _set_cache(self, key: str, value: Any):
        """Set item in cache"""
        self._cache[key] = (value, datetime.now())
    
    def cleanup_expired_cache(self):
        """Remove expired entries from search cache"""
        try:
            with sqlite3.connect(self.db_path) as conn:
                cursor = conn.cursor()
                cursor.execute('''
                    DELETE FROM search_cache 
                    WHERE expires_at < ?
                ''', (datetime.now().isoformat(),))
                conn.commit()
                
        except Exception as e:
            self.logger.error(f"Cache cleanup failed: {e}")

# Example usage and testing
if __name__ == "__main__":
    # Test the knowledge base manager
    kb = KnowledgeBaseManager()
    
    # Test technique storage
    test_technique = {
        'name': 'D3D12_RESOURCE_BARRIER',
        'category': 'Core',
        'description': 'Synchronizes access to resources between GPU operations',
        'relevance_score': 0.9,
        'project_applications': ['UAV barriers before compute dispatch'],
        'tags': ['synchronization', 'barrier', 'performance'],
        'examples': ['UAV barriers before compute dispatch']
    }
    
    technique_id = kb.store_technique(test_technique)
    print(f"Stored technique with ID: {technique_id}")
    
    # Test search
    results = kb.search_techniques("barrier", limit=5)
    print(f"Found {len(results)} results for 'barrier'")
    
    # Test statistics
    stats = kb.get_statistics()
    print(f"Knowledge base statistics: {stats}")



